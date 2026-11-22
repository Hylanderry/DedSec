// DedSec Fork — DX12 Native Pipeline
// FSR3 Present Hook Implementation — intercepts IDXGISwapChain::Present
// Built from scratch for the DedSec DX12 native pipeline.
//
// This file implements the PresentHook class declared in dedsec_fsr3_hook.h.
// It installs a vtable hook on IDXGISwapChain::Present to inject FSR3
// upscaling and frame generation passes before the real Present executes.

#define DEDSEC_FSR3_HOOK_IMPLEMENTATION
#include "dedsec_fsr3_hook.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <atomic>
#include <cstdio>
#include <mutex>

namespace dedsec {
namespace fsr3 {

// ─── Global state ─────────────────────────────────────────────────────────────
static std::mutex g_hook_mutex;
static FrameGenerator g_frame_generator;
static bool g_hook_installed = false;
static IDXGISwapChain3* g_swapchain = nullptr;
static ID3D12Device* g_device = nullptr;

// Original Present function pointer (saved before hooking)
typedef HRESULT(__stdcall *PresentFunc)(IDXGISwapChain*, UINT, UINT);
static PresentFunc g_original_present = nullptr;

// Vtable index for IDXGISwapChain::Present is 8
static constexpr unsigned kPresentVtableIndex = 8;

// ─── Hooked Present implementation ────────────────────────────────────────────
static HRESULT __stdcall hooked_present(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags) {
    // Skip if DXGI_PRESENT_TEST (query only, no actual present)
    if (flags & DXGI_PRESENT_TEST) {
        return g_original_present(swapchain, sync_interval, flags);
    }

    // Try to get the back buffer for FSR processing
    ID3D12Resource* back_buffer = nullptr;
    auto* sc3 = static_cast<IDXGISwapChain3*>(swapchain);

    // Get current back buffer index and resource
    UINT back_buffer_index = sc3->GetCurrentBackBufferIndex();
    HRESULT hr = sc3->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer));

    if (SUCCEEDED(hr) && back_buffer != nullptr) {
        // Run FSR3 upscaling + optional frame generation
        // Note: In a full implementation, we'd also need depth buffer and motion vectors
        // from the game's rendering pipeline. For now, we pass nullptr which triggers
        // spatial-only upscaling mode.
        //
        // TODO: Hook into the game's command queue to get proper depth/motion data.
        // The Xbox D3D12 proxy will eventually expose these through our shim layer.

        // For now, just track stats without actual processing until we have
        // the full resource binding wired up
        // g_frame_generator.on_present(cmd_queue, back_buffer, nullptr, nullptr);

        back_buffer->Release();
    }

    // Call the original Present
    return g_original_present(swapchain, sync_interval, flags);
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool PresentHook::install(ID3D12Device* device, IDXGISwapChain3* swapchain) {
    std::lock_guard lock(g_hook_mutex);

    if (g_hook_installed) return true;
    if (!device || !swapchain) return false;

    g_device = device;
    g_swapchain = swapchain;

    // Initialize the frame generator
    DXGI_SWAP_CHAIN_DESC1 desc;
    if (FAILED(swapchain->GetDesc1(&desc))) return false;

    if (!g_frame_generator.initialize(device, desc)) {
        std::fprintf(stderr, "[DedSec FSR3] Failed to initialize frame generator\n");
        return false;
    }

    // Save original Present from vtable
    void** vtable = *reinterpret_cast<void***>(swapchain);
    g_original_present = reinterpret_cast<PresentFunc>(vtable[kPresentVtableIndex]);

    // NOTE: Actual vtable patching requires VirtualProtect + write permissions.
    // For the DedSec proxy approach, we instead intercept at the proxy level
    // in d3d12_native_device.cpp rather than patching the real swapchain vtable.
    // This is safer and doesn't conflict with anti-cheat or DRM systems.
    //
    // The proxy's Present thunk calls PresentHook::on_pre_present() before
    // forwarding to the real swapchain.

    g_hook_installed = true;
    std::fprintf(stdout, "[DedSec FSR3] Present hook installed (%ux%u → %ux%u)\n",
                 desc.Width, desc.Height, desc.Width, desc.Height);
    return true;
}

void PresentHook::uninstall() {
    std::lock_guard lock(g_hook_mutex);

    if (!g_hook_installed) return;

    g_frame_generator.shutdown();
    g_original_present = nullptr;
    g_swapchain = nullptr;
    g_device = nullptr;
    g_hook_installed = false;

    std::fprintf(stdout, "[DedSec FSR3] Present hook uninstalled\n");
}

bool PresentHook::on_pre_present(IDXGISwapChain3* swapchain, UINT sync_interval, UINT flags) {
    if (!g_hook_installed || !g_swapchain) return false;
    if (flags & DXGI_PRESENT_TEST) return false;

    // Get back buffer
    ID3D12Resource* back_buffer = nullptr;
    UINT idx = swapchain->GetCurrentBackBufferIndex();
    if (FAILED(swapchain->GetBuffer(idx, IID_PPV_ARGS(&back_buffer)))) return false;

    // TODO: Obtain command queue, depth buffer, and motion vectors from the
    // proxy device's tracked state. These are needed for temporal upscaling
    // and frame generation. Spatial-only mode can work with just the color buffer.
    //
    // For the initial integration, we log stats but don't modify the frame.
    // Full integration requires wiring into d3d12_native_device.cpp's
    // ExecuteCommandLists to capture the relevant resources.

    back_buffer->Release();
    return false; // Passthrough for now
}

FrameGenerator* PresentHook::get_generator() {
    return g_hook_installed ? &g_frame_generator : nullptr;
}

bool PresentHook::is_active() {
    return g_hook_installed;
}

} // namespace fsr3
} // namespace dedsec