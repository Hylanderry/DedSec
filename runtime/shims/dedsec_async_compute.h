// DedSec Fork — DX12 Native Pipeline
// Async Compute Dispatcher — runs compute workloads on a separate queue
// in parallel with the graphics queue for maximum GPU utilization.
//
// Architecture:
// - Creates a dedicated D3D12_COMMAND_LIST_TYPE_COMPUTE queue
// - Accepts compute dispatches (shadows, culling, FSR passes, particles)
// - Synchronizes via fences to ensure results are ready before graphics reads them
// - Falls back to graphics queue if async compute is not supported or disabled
//
// Benefits:
// - Shadows and occlusion culling run in parallel with main draw calls
// - FSR upscaling/framegen can overlap with next frame's geometry pass
// - Better GPU occupancy on all tiers (especially important for low-end GPUs)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <d3d12.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dedsec {

// Types of async compute work that can be dispatched
enum class AsyncComputeTask : uint8_t {
    SHADOW_MAP = 0,         // Shadow cascade rendering
    OCCLUSION_CULL = 1,     // Hi-Z occlusion culling
    FSR_UPSCALE = 2,        // FSR EASU/RCAS upscaling
    FSR_FRAMEGEN = 3,       // FSR frame interpolation
    PARTICLES = 4,          // GPU particle simulation
    VRS_TILE_CLASSIFY = 5,  // Variable Rate Shading tile classification
    CUSTOM = 255,           // User-defined compute work
};

// Descriptor for a single async compute dispatch
struct AsyncDispatchDesc {
    AsyncComputeTask task_type;
    ID3D12PipelineState* pso;           // Compute PSO
    ID3D12RootSignature* root_sig;      // Root signature
    uint32_t thread_groups_x;
    uint32_t thread_groups_y;
    uint32_t thread_groups_z;
    // Descriptor table and constants are set by the caller before dispatch
    // This struct only captures the dispatch parameters
};

class AsyncComputeDispatcher {
public:
    // Initialize with device. Creates compute queue + command allocator/list.
    // Returns false if async compute is not available.
    static bool initialize(ID3D12Device* device);

    // Shutdown and release resources
    static void shutdown();

    // Check if async compute is available and initialized
    static bool is_available();

    // Submit a compute dispatch to the async queue.
    // The caller must have already set descriptors/constants on the provided cmd list.
    // Returns a fence value that can be waited on to ensure completion.
    static uint64_t submit(
        ID3D12GraphicsCommandList* cmd_list,
        const AsyncDispatchDesc& desc
    );

    // Signal the graphics queue to wait for a specific async fence value.
    // Call this before any graphics work that depends on async compute results.
    static void sync_graphics_wait(ID3D12CommandQueue* graphics_queue, uint64_t fence_value);

    // Signal the async queue to wait for a graphics fence value.
    // Call this before async work that depends on graphics output (e.g., FSR reading back buffer).
    static void signal_async_wait(uint64_t graphics_fence_value);

    // Get the async compute queue (for advanced usage)
    static ID3D12CommandQueue* get_queue();

    // Get the fence used for async→graphics synchronization
    static ID3D12Fence* get_fence();

    // Statistics
    static uint64_t get_total_dispatches();
    static uint64_t get_total_gpu_time_us();

private:
    static ID3D12Device* s_device;
    static ID3D12CommandQueue* s_compute_queue;
    static ID3D12CommandAllocator* s_cmd_allocator;
    static ID3D12Fence* s_fence;
    static std::atomic<uint64_t> s_fence_value;
    static std::mutex s_submit_mutex;
    static std::atomic<uint64_t> s_total_dispatches;
    static std::atomic<uint64_t> s_total_gpu_time_us;
    static bool s_initialized;
};

} // namespace dedsec