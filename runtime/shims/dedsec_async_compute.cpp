// DedSec Fork — DX12 Native Pipeline
// Async Compute Dispatcher — Implementation
// Built from scratch for the DedSec DX12 native pipeline.
//
// Creates a dedicated compute queue and manages fence synchronization
// between async compute and graphics workloads. All submissions are
// serialized through a mutex to ensure correct fence ordering.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_ASYNC_COMPUTE_IMPLEMENTATION
#include "dedsec_async_compute.h"

#include <cstdio>

namespace dedsec {

// ─── Static state ─────────────────────────────────────────────────────────────
ID3D12Device* AsyncComputeDispatcher::s_device = nullptr;
ID3D12CommandQueue* AsyncComputeDispatcher::s_compute_queue = nullptr;
ID3D12CommandAllocator* AsyncComputeDispatcher::s_cmd_allocator = nullptr;
ID3D12Fence* AsyncComputeDispatcher::s_fence = nullptr;
std::atomic<uint64_t> AsyncComputeDispatcher::s_fence_value{0};
std::mutex AsyncComputeDispatcher::s_submit_mutex;
std::atomic<uint64_t> AsyncComputeDispatcher::s_total_dispatches{0};
std::atomic<uint64_t> AsyncComputeDispatcher::s_total_gpu_time_us{0};
bool AsyncComputeDispatcher::s_initialized = false;

// ─── Public API ───────────────────────────────────────────────────────────────

bool AsyncComputeDispatcher::initialize(ID3D12Device* device) {
    if (s_initialized) return true;
    if (!device) return false;

    s_device = device;
    s_device->AddRef();

    // Create compute command queue
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 0;

    HRESULT hr = s_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&s_compute_queue));
    if (FAILED(hr)) {
        std::fprintf(stderr, "[DedSec Async] Failed to create compute queue (hr=0x%08x)\n",
                     static_cast<unsigned>(hr));
        s_device->Release();
        s_device = nullptr;
        return false;
    }

    // Create command allocator for async compute
    hr = s_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&s_cmd_allocator));
    if (FAILED(hr)) {
        std::fprintf(stderr, "[DedSec Async] Failed to create command allocator (hr=0x%08x)\n",
                     static_cast<unsigned>(hr));
        s_compute_queue->Release();
        s_compute_queue = nullptr;
        s_device->Release();
        s_device = nullptr;
        return false;
    }

    // Create fence for async→graphics synchronization
    hr = s_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_fence));
    if (FAILED(hr)) {
        std::fprintf(stderr, "[DedSec Async] Failed to create fence (hr=0x%08x)\n",
                     static_cast<unsigned>(hr));
        s_cmd_allocator->Release();
        s_cmd_allocator = nullptr;
        s_compute_queue->Release();
        s_compute_queue = nullptr;
        s_device->Release();
        s_device = nullptr;
        return false;
    }

    s_fence_value.store(0);
    s_total_dispatches.store(0);
    s_total_gpu_time_us.store(0);
    s_initialized = true;

    std::printf("[DedSec Async] Compute queue initialized\n");
    return true;
}

void AsyncComputeDispatcher::shutdown() {
    if (!s_initialized) return;

    // Wait for all pending work to complete
    if (s_compute_queue && s_fence) {
        uint64_t val = s_fence_value.load();
        if (val > 0) {
            s_compute_queue->Signal(s_fence, val);
            // Busy-wait briefly then block
            while (s_fence->GetCompletedValue() < val) {
                // In production, use SetEventOnCompletion + WaitForSingleObject
                // For now, spin with yield
                SwitchToThread();
            }
        }
    }

    if (s_fence) { s_fence->Release(); s_fence = nullptr; }
    if (s_cmd_allocator) { s_cmd_allocator->Release(); s_cmd_allocator = nullptr; }
    if (s_compute_queue) { s_compute_queue->Release(); s_compute_queue = nullptr; }
    if (s_device) { s_device->Release(); s_device = nullptr; }

    s_initialized = false;
    std::printf("[DedSec Async] Shutdown: %llu dispatches, %llu us GPU time\n",
                static_cast<unsigned long long>(s_total_dispatches.load()),
                static_cast<unsigned long long>(s_total_gpu_time_us.load()));
}

bool AsyncComputeDispatcher::is_available() {
    return s_initialized;
}

uint64_t AsyncComputeDispatcher::submit(
    ID3D12GraphicsCommandList* cmd_list,
    const AsyncDispatchDesc& desc)
{
    if (!s_initialized || !cmd_list || !desc.pso) return 0;

    std::lock_guard lock(s_submit_mutex);

    // Reset allocator and record the dispatch
    s_cmd_allocator->Reset();

    // Note: In a full implementation, we'd create our own command list here.
    // For now, the caller provides a pre-recorded command list that already
    // has the PSO, root signature, descriptors and Dispatch() call recorded.
    // We just execute it on the compute queue and signal the fence.

    ID3D12CommandList* lists[] = { cmd_list };
    s_compute_queue->ExecuteCommandLists(1, lists);

    // Signal fence with next value
    uint64_t fence_val = ++s_fence_value;
    s_compute_queue->Signal(s_fence, fence_val);

    s_total_dispatches.fetch_add(1);

    return fence_val;
}

void AsyncComputeDispatcher::sync_graphics_wait(
    ID3D12CommandQueue* graphics_queue, uint64_t fence_value)
{
    if (!s_initialized || !graphics_queue || !s_fence || fence_value == 0) return;

    // Make the graphics queue wait until the async compute fence reaches this value
    graphics_queue->Wait(s_fence, fence_value);
}

void AsyncComputeDispatcher::signal_async_wait(uint64_t graphics_fence_value) {
    // This would require access to the graphics queue's fence.
    // In practice, the game's rendering code signals its own fence after
    // completing graphics work, and we call compute_queue->Wait(gfx_fence, val).
    // This is a placeholder for the integration point.
    (void)graphics_fence_value;
}

ID3D12CommandQueue* AsyncComputeDispatcher::get_queue() {
    return s_compute_queue;
}

ID3D12Fence* AsyncComputeDispatcher::get_fence() {
    return s_fence;
}

uint64_t AsyncComputeDispatcher::get_total_dispatches() {
    return s_total_dispatches.load();
}

uint64_t AsyncComputeDispatcher::get_total_gpu_time_us() {
    return s_total_gpu_time_us.load();
}

} // namespace dedsec