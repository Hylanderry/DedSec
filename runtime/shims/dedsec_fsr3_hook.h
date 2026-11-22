// DedSec Fork — DX12 Native Pipeline
// FSR3 Present Hook — intercepts IDXGISwapChain::Present to inject FSR passes
//
// This header provides the hook that wires dedsec::fsr3::FrameGenerator into
// the D3D12 native device proxy's swapchain Present() call. It is included by
// d3d12_native_device.cpp and called from the vtable thunk for Present.
//
// Architecture:
//   Game renders frame → calls Present() → our hook intercepts →
//   FrameGenerator::on_present() runs upscale + optional framegen →
//   modified back buffer is presented to screen
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include "dedsec_fsr3.h"

#include <d3d12.h>
#include <dxgi1_6.h>

namespace dedsec {
namespace fsr3 {

// Global frame generator instance (one per process, tied to the primary swapchain)
class PresentHook {
public:
    // Call once after device and swapchain are created
    static bool install(ID3D12Device* device, IDXGISwapChain3* swapchain);

    // Call before device shutdown
    static void uninstall();

    // Called from the Present vtable thunk BEFORE the real Present executes.
    // Returns true if FSR processing was applied, false for passthrough.
    static bool on_pre_present(IDXGISwapChain3* swapchain, UINT sync_interval, UINT flags);

    // Get the global frame generator (for config changes from settings UI)
    static FrameGenerator* get_generator();

    // Check if the hook is installed and active
    static bool is_active();
};

} // namespace fsr3
} // namespace dedsec