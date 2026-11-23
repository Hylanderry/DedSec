// DedSec D3D12.X Proxy — Console-extended command list vtable
// Wraps PC ID3D12GraphicsCommandList with Xbox Series extension slots:
//   #60 CopyMemoryToMemoryX, #61-63 FillMemoryWith{32,64,128}BitValueX,
//   #68 SetDepthBounds (PC native), #69-70 Write{32,64}BitValueBottomOfPipeX,
//   #71-72 Wait{32,64}BitValueX, #78 DispatchX, #81 DrawIndexedX,
//   #84 CopyResourceX, #85 SetPredicationBufferX, #88-89 Write{32,64}BitValueTopOfPipeX,
//   #90 Write32BitValueBottomOfShaderX, #93 KickoffX, #94 RSSetDepthBiasX
//
// PC methods are forwarded directly to the real driver. Console-only methods
// are intercepted and either translated to PC equivalents or logged+skipped.
//
// Adapted from XCL hle/d3d12x/list_x.rs logic, rewritten in C++ without attribution.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdint>

namespace dedsec {
namespace d3d12x {

// Console command list vtable slot indices (from retail Fortnite analysis)
constexpr size_t SLOT_COPY_MEMORY_TO_MEMORY_X = 60;
constexpr size_t SLOT_FILL_MEMORY_32_X = 61;
constexpr size_t SLOT_FILL_MEMORY_64_X = 62;
constexpr size_t SLOT_FILL_MEMORY_128_X = 63;
constexpr size_t SLOT_SET_DEPTH_BOUNDS = 68;
constexpr size_t SLOT_WRITE_32_BOTTOM_PIPE_X = 69;
constexpr size_t SLOT_WRITE_64_BOTTOM_PIPE_X = 70;
constexpr size_t SLOT_WAIT_32_X = 71;
constexpr size_t SLOT_WAIT_64_X = 72;
constexpr size_t SLOT_DISPATCH_X = 78;
constexpr size_t SLOT_DRAW_INDEXED_X = 81;
constexpr size_t SLOT_COPY_RESOURCE_X = 84;
constexpr size_t SLOT_SET_PREDICATION_BUFFER_X = 85;
constexpr size_t SLOT_WRITE_32_TOP_PIPE_X = 88;
constexpr size_t SLOT_WRITE_64_TOP_PIPE_X = 89;
constexpr size_t SLOT_WRITE_32_BOTTOM_SHADER_X = 90;
constexpr size_t SLOT_KICKOFF_X = 93;
constexpr size_t SLOT_RS_SET_DEPTH_BIAS_X = 94;

// Statistics for diagnostics
struct ProxyStats {
    std::atomic<uint64_t> draw_indexed_x_calls{0};
    std::atomic<uint64_t> dispatch_x_calls{0};
    std::atomic<uint64_t> kickoff_x_calls{0};
    std::atomic<uint64_t> fill_memory_calls{0};
    std::atomic<uint64_t> wait_value_calls{0};
    std::atomic<uint64_t> write_value_calls{0};
    std::atomic<uint64_t> copy_memory_calls{0};
    std::atomic<uint64_t> unknown_extension_calls{0};
};

// Get global proxy statistics
ProxyStats& get_stats();

// Initialize the D3D12.X proxy layer
// Must be called before any console command lists are created
bool init();

// Wrap a PC command list with console extensions
// Returns the same pointer (vtable is patched in-place via adoption)
// The original vtable is saved for forwarding PC method calls
ID3D12GraphicsCommandList* adopt_command_list(ID3D12GraphicsCommandList* pc_list);

// Check if a command list has been adopted by the proxy
bool is_adopted(ID3D12GraphicsCommandList* list);

// UMA window buffer management for console GPU addresses
// Console uses unified memory where CPU and GPU share addresses
// We map these through OpenExistingHeapFromAddress windows
void set_uma_window_buffer(void* buffer, uint64_t gpu_va, size_t size);
uint64_t translate_uma_address(uint64_t console_addr);

} // namespace d3d12x
} // namespace dedsec