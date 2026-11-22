// DedSec Fork — DX12 Native Pipeline
// FSR 3.1 Upscaling + Frame Generation — Implementation
// Built from scratch for the DedSec DX12 native pipeline.
//
// This file implements the FrameGenerator class declared in dedsec_fsr3.h.
// It manages GPU resources, shader compilation via DXC, and the upscaling/framegen
// dispatch logic that hooks into Present() on the native DX12 device proxy.

#define DEDSEC_FSR3_IMPLEMENTATION
#include "dedsec_fsr3.h"

#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

// DedSec: DXC compiler interface for runtime HLSL→DXIL compilation
// In production, link against dxcompiler.lib or load dxcompiler.dll dynamically.
// For now, we define the interfaces we need to avoid hard dependency at compile time.

namespace dedsec {
namespace fsr3 {

// ─── Render scale factors per quality mode ────────────────────────────────────
static constexpr float kQualityScales[] = {
    1.0f,   // NativeAA
    1.5f,   // Quality
    1.7f,   // Balanced
    2.0f,   // Performance
    3.0f,   // UltraPerformance
};

static_assert(sizeof(kQualityScales) / sizeof(kQualityScales[0]) == 5,
              "Must match QualityMode enum count");

// ─── Internal implementation ──────────────────────────────────────────────────
class FrameGeneratorImpl {
public:
    FrameGeneratorImpl() = default;
    ~FrameGeneratorImpl() { shutdown(); }

    bool initialize(ID3D12Device* device, const DXGI_SWAP_CHAIN_DESC1& desc) {
        if (!device) return false;

        device_ = device;
        device_->AddRef();

        display_width_ = desc.Width;
        display_height_ = desc.Height;
        format_ = desc.Format;

        // Create command allocator and list for FSR passes
        HRESULT hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_allocator_));
        if (FAILED(hr)) return false;

        hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         cmd_allocator_, nullptr,
                                         IID_PPV_ARGS(&cmd_list_));
        if (FAILED(hr)) return false;

        cmd_list_->Close();

        // Create descriptor heaps for FSR resources
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors = 64; // Enough for FSR internal textures
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_uav_heap_));
        if (FAILED(hr)) return false;

        // Initialize stats
        std::memset(&stats_, 0, sizeof(stats_));
        initialized_ = true;
        return true;
    }

    void shutdown() {
        if (cmd_list_) { cmd_list_->Release(); cmd_list_ = nullptr; }
        if (cmd_allocator_) { cmd_allocator_->Release(); cmd_allocator_ = nullptr; }
        if (srv_uav_heap_) { srv_uav_heap_->Release(); srv_uav_heap_ = nullptr; }
        if (upscale_pso_) { upscale_pso_->Release(); upscale_pso_ = nullptr; }
        if (framegen_pso_) { framegen_pso_->Release(); framegen_pso_ = nullptr; }
        if (device_) { device_->Release(); device_ = nullptr; }
        initialized_ = false;
    }

    bool is_initialized() const { return initialized_; }

    void set_config(const Config& config) {
        std::lock_guard lock(config_mutex_);
        config_ = config;

        // Recompute render resolution if display size changed
        if (config.render_width > 0 && config.render_height > 0) {
            current_render_w_ = config.render_width;
            current_render_h_ = config.render_height;
        } else {
            get_render_resolution(display_width_, display_height_,
                                  config.quality,
                                  current_render_w_, current_render_h_);
        }
    }

    Config get_config() const {
        std::lock_guard lock(config_mutex_);
        return config_;
    }

    bool on_present(ID3D12CommandQueue* cmd_queue,
                    ID3D12Resource* back_buffer,
                    ID3D12Resource* depth_buffer,
                    ID3D12Resource* motion_vectors,
                    ID3D12Resource* hud_less) {
        if (!initialized_ || !cmd_queue || !back_buffer) return false;

        Config cfg;
        {
            std::lock_guard lock(config_mutex_);
            cfg = config_;
        }

        // Determine if frame generation should be active this frame
        bool fg_active = false;
        switch (cfg.frame_gen) {
            case FrameGenMode::On:
                fg_active = true;
                break;
            case FrameGenMode::Auto:
                // Enable FG when effective FPS < target (or display refresh)
                fg_active = stats_.effective_fps > 0 &&
                            stats_.effective_fps < static_cast<double>(cfg.target_fps > 0 ? cfg.target_fps : 60);
                break;
            case FrameGenMode::Off:
            default:
                fg_active = false;
                break;
        }

        // Reset command allocator and list
        cmd_allocator_->Reset();
        cmd_list_->Reset(cmd_allocator_, nullptr);

        // ── Upscaling pass ────────────────────────────────────────────────
        // In a full implementation, this would:
        // 1. Transition back_buffer to UAV state
        // 2. Bind FSR upscaling compute shader PSO
        // 3. Dispatch compute shader with color, depth, motion vectors as inputs
        // 4. Transition back_buffer back to RTV/Present state
        //
        // For now, we record the barrier transitions as placeholders.
        // The actual shader dispatch will be wired once the HLSL shaders are compiled.

        D3D12_RESOURCE_BARRIER pre_upscale = {};
        pre_upscale.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        pre_upscale.Transition.pResource = back_buffer;
        pre_upscale.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pre_upscale.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        pre_upscale.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmd_list_->ResourceBarrier(1, &pre_upscale);

        // TODO: Dispatch FSR3 EASU/RCAS compute shader here
        // This is where the actual upscaling happens:
        // - EASU (Edge Adaptive Spatial Upsampling) for spatial mode
        // - Temporal accumulation + reprojection for temporal mode
        // - RCAS (Robust Contrast Adaptive Sharpening) as post-pass

        D3D12_RESOURCE_BARRIER post_upscale = {};
        post_upscale.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        post_upscale.Transition.pResource = back_buffer;
        post_upscale.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        post_upscale.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        post_upscale.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmd_list_->ResourceBarrier(1, &post_upscale);

        // ── Frame generation pass (if active) ─────────────────────────────
        if (fg_active && motion_vectors != nullptr) {
            // Frame generation interpolates between the current and previous frame
            // using motion vectors to create a synthetic intermediate frame.
            //
            // In a full implementation:
            // 1. Copy current frame to history buffer
            // 2. Compute optical flow from motion vectors
            // 3. Dispatch frame interpolation compute shader
            // 4. Apply jitter correction and ghosting reduction
            //
            // Placeholder barrier for frame gen output:
            D3D12_RESOURCE_BARRIER fg_barrier = {};
            fg_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            fg_barrier.Transition.pResource = back_buffer;
            fg_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            fg_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            fg_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            cmd_list_->ResourceBarrier(1, &fg_barrier);

            // TODO: Dispatch FSR3 Frame Generation compute shader
            // Inputs: current frame, previous frame, motion vectors, depth
            // Output: interpolated frame written to back_buffer

            D3D12_RESOURCE_BARRIER fg_post = {};
            fg_post.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            fg_post.Transition.pResource = back_buffer;
            fg_post.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            fg_post.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            fg_post.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            cmd_list_->ResourceBarrier(1, &fg_post);

            stats_.frames_generated++;
        }

        cmd_list_->Close();

        // Execute the FSR command list on the game's queue
        ID3D12CommandList* lists[] = { cmd_list_ };
        cmd_queue->ExecuteCommandLists(1, lists);

        // Update stats
        stats_.frames_real++;
        stats_.frame_gen_active = fg_active;
        stats_.active_quality = cfg.quality;

        return true;
    }

    Stats get_stats() const { return stats_; }

    static void get_render_resolution(uint32_t display_w, uint32_t display_h,
                                       QualityMode mode,
                                       uint32_t& out_w, uint32_t& out_h) {
        float scale = kQualityScales[static_cast<uint8_t>(mode)];
        out_w = static_cast<uint32_t>(std::round(display_w / scale));
        out_h = static_cast<uint32_t>(std::round(display_h / scale));
        // Ensure minimum 640x360
        out_w = std::max(out_w, 640u);
        out_h = std::max(out_h, 360u);
    }

private:
    ID3D12Device* device_ = nullptr;
    ID3D12CommandAllocator* cmd_allocator_ = nullptr;
    ID3D12GraphicsCommandList* cmd_list_ = nullptr;
    ID3D12DescriptorHeap* srv_uav_heap_ = nullptr;
    ID3D12PipelineState* upscale_pso_ = nullptr;
    ID3D12PipelineState* framegen_pso_ = nullptr;

    uint32_t display_width_ = 0;
    uint32_t display_height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    uint32_t current_render_w_ = 0;
    uint32_t current_render_h_ = 0;

    mutable std::mutex config_mutex_;
    Config config_ = {};
    Stats stats_ = {};
    bool initialized_ = false;
};

// ─── Public API implementation ────────────────────────────────────────────────

FrameGenerator::FrameGenerator() : impl_(std::make_unique<FrameGeneratorImpl>()) {}
FrameGenerator::~FrameGenerator() = default;

bool FrameGenerator::initialize(ID3D12Device* device, const DXGI_SWAP_CHAIN_DESC1& desc) {
    return impl_->initialize(device, desc);
}

void FrameGenerator::shutdown() { impl_->shutdown(); }
bool FrameGenerator::is_initialized() const { return impl_->is_initialized(); }
void FrameGenerator::set_config(const Config& config) { impl_->set_config(config); }
Config FrameGenerator::get_config() const { return impl_->get_config(); }

bool FrameGenerator::on_present(ID3D12CommandQueue* cmd_queue,
                                 ID3D12Resource* back_buffer,
                                 ID3D12Resource* depth_buffer,
                                 ID3D12Resource* motion_vectors,
                                 ID3D12Resource* hud_less) {
    return impl_->on_present(cmd_queue, back_buffer, depth_buffer, motion_vectors, hud_less);
}

Stats FrameGenerator::get_stats() const { return impl_->get_stats(); }

void FrameGenerator::get_render_resolution(uint32_t display_w, uint32_t display_h,
                                            QualityMode mode,
                                            uint32_t& out_w, uint32_t& out_h) {
    FrameGeneratorImpl::get_render_resolution(display_w, display_h, mode, out_w, out_h);
}

QualityMode recommend_quality_mode(ID3D12Device* device, uint32_t display_w, uint32_t display_h) {
    if (!device) return QualityMode::Balanced;

    // Query adapter info to determine GPU tier
    // For now, use display resolution as heuristic:
    // 4K+ → Performance or UltraPerformance
    // 1440p → Balanced
    // 1080p → Quality
    uint32_t pixels = display_w * display_h;
    if (pixels >= 3840 * 2160) return QualityMode::Performance;
    if (pixels >= 2560 * 1440) return QualityMode::Balanced;
    return QualityMode::Quality;
}

} // namespace fsr3
} // namespace dedsec