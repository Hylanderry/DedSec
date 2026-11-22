// DedSec Fork — DX12 Native Pipeline
// FSR 3.1 Upscaling + Frame Generation Integration for GTA VI Xbox→PC Port
//
// Architecture:
//   - Intercepts Present() calls in the D3D12 native device proxy
//   - Injects FSR3 upscaling pass before present (spatial or temporal)
//   - Injects Frame Generation pass between real frames for smooth interpolation
//   - Uses AMD FidelityFX SDK headers (open-source, MIT licensed)
//   - Falls back to bilinear upscale if FSR3 not available on hardware
//
// This is NOT a copy of any existing implementation. Built from scratch for
// the DedSec DX12 native pipeline, optimized for NVIDIA/AMD Windows GPUs.
//
// Usage:
//   dedsec::fsr3::FrameGenerator fg;
//   fg.initialize(device, swapchain_desc);
//   fg.on_present(command_queue, back_buffer, depth_buffer, motion_vectors);
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>
#include <string>

namespace dedsec {
namespace fsr3 {

// Quality presets matching FSR 3.1 specification
enum class QualityMode : uint8_t {
    NativeAA = 0,      // 1.0x render scale, AA only
    Quality = 1,       // 1.5x upscale (e.g., 1440p → 4K)
    Balanced = 2,      // 1.7x upscale
    Performance = 3,   // 2.0x upscale (e.g., 1080p → 4K)
    UltraPerformance = 4, // 3.0x upscale
};

// Frame generation mode
enum class FrameGenMode : uint8_t {
    Off = 0,           // No frame generation, upscaling only
    On = 1,            // Full frame generation (interpolated frames)
    Auto = 2,          // Enable when FPS < display refresh rate
};

// Configuration for the FSR3 integration
struct Config {
    QualityMode quality = QualityMode::Balanced;
    FrameGenMode frame_gen = FrameGenMode::Auto;
    bool sharpness_enabled = true;
    float sharpness_strength = 0.5f;     // 0.0 - 1.0
    bool hdr_enabled = false;
    bool debug_overlay = false;

    // Render resolution (before upscaling)
    uint32_t render_width = 0;
    uint32_t render_height = 0;

    // Display resolution (after upscaling)
    uint32_t display_width = 0;
    uint32_t display_height = 0;

    // Frame generation target FPS (0 = auto from display refresh)
    uint32_t target_fps = 0;
};

// Statistics exposed for overlay/debugging
struct Stats {
    double gpu_time_ms_upscale = 0.0;
    double gpu_time_ms_framegen = 0.0;
    uint32_t frames_generated = 0;
    uint32_t frames_real = 0;
    double effective_fps = 0.0;
    double input_latency_ms = 0.0;
    bool frame_gen_active = false;
    QualityMode active_quality = QualityMode::Balanced;
};

// Forward declarations
class FrameGeneratorImpl;

// Main FSR3 integration class
// Thread-safe: can be called from any thread, internally synchronizes GPU work
class FrameGenerator {
public:
    FrameGenerator();
    ~FrameGenerator();

    // Non-copyable, non-movable
    FrameGenerator(const FrameGenerator&) = delete;
    FrameGenerator& operator=(const FrameGenerator&) = delete;

    // Initialize with the DX12 device and swapchain description.
    // Must be called once after device creation, before any Present() calls.
    // Returns true on success.
    bool initialize(ID3D12Device* device, const DXGI_SWAP_CHAIN_DESC1& swapchain_desc);

    // Shutdown and release all GPU resources.
    void shutdown();

    // Check if initialized successfully.
    bool is_initialized() const;

    // Update configuration (can be called at runtime, e.g., from settings menu).
    // Changes take effect on the next frame.
    void set_config(const Config& config);
    Config get_config() const;

    // Called BEFORE the game's Present() call.
    // Injects upscaling and/or frame generation passes.
    //
    // Parameters:
    //   cmd_queue      - The D3D12 command queue used for rendering
    //   back_buffer    - The swapchain back buffer (will be upscaled into)
    //   depth_buffer   - Depth/stencil buffer for temporal reprojection
    //   motion_vectors - Motion vector buffer (RG16_FLOAT, screen-space)
    //                    If null, spatial upscaling is used instead of temporal
    //   hud_less       - HUD-less color buffer for UI-free upscaling (optional)
    //                    If null, standard color buffer is used
    //
    // Returns true if the frame was processed (upscaled/generated).
    // Returns false if passthrough (no processing needed this frame).
    bool on_present(
        ID3D12CommandQueue* cmd_queue,
        ID3D12Resource* back_buffer,
        ID3D12Resource* depth_buffer,
        ID3D12Resource* motion_vectors,
        ID3D12Resource* hud_less = nullptr
    );

    // Get current statistics for overlay display.
    Stats get_stats() const;

    // Get the recommended render resolution for a given display resolution and quality mode.
    static void get_render_resolution(
        uint32_t display_width,
        uint32_t display_height,
        QualityMode mode,
        uint32_t& out_render_width,
        uint32_t& out_render_height
    );

private:
    std::unique_ptr<FrameGeneratorImpl> impl_;
};

// Helper: compute optimal quality mode based on GPU tier and display resolution
QualityMode recommend_quality_mode(ID3D12Device* device, uint32_t display_width, uint32_t display_height);

} // namespace fsr3
} // namespace dedsec