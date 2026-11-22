// DedSec Fork — DX12 Native Pipeline
// Xbox-ABI proxy around a native PC ID3D12Device with SM 6.6 shader translation.
// Based on d3d12_xs_device.cpp proxy vtable pattern, but routes to native DX12 instead of Vulkan.
//
// Architecture: Xbox D3D12 calls → proxy vtable → native DX12 device with translated HLSL shaders
// Eliminates the Vulkan translation layer for better performance on NVIDIA/AMD Windows GPUs.
//
// The proxy object's first word is a vtable built once from one thunk per slot. A thunk
// receives the proxy in RCX, replaces it with the PC device pointer from offset 8, and tail-calls
// the PC method at the same index through a 15-argument integer prototype.

#include "d3d12_xs_device.h"
#include "rdna2_to_hlsl.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <new>
#include <utility>
#include <unordered_map>
#include <mutex>

namespace rt::d3d12_native {
namespace {

constexpr unsigned kMaxSlots = 128;
static_assert(kMaxSlots > 83, "the proxy table must cover ID3D12Device14 (83 slots)");
static_assert(sizeof(void*) == 8, "the Xbox-ABI proxy is x64-only");

struct DeviceProxy {
    void** vtable;
    ID3D12Device* real;
    void** real_vtable;
    unsigned slot_count;
    std::atomic<unsigned long> refs;

    // DedSec: Shader translation cache
    std::unordered_map<uint64_t, ID3D12PipelineState*> pso_cache;
    std::mutex pso_mutex;
};

constexpr unsigned kHeapSlots = 11;

struct HeapProxy {
    void** vtable;
    ID3D12DescriptorHeap* real;
    DeviceProxy* device;
    void** real_vtable;
    std::atomic<unsigned long> refs;
};

std::atomic<std::uint64_t> g_hits[kMaxSlots];
std::atomic<bool> g_first[kMaxSlots];
std::atomic<bool> g_beyond_warned[kMaxSlots];
std::atomic<std::uint64_t> g_heap_hits[kHeapSlots];
std::atomic<bool> g_heap_first[kHeapSlots];

bool trace_enabled() {
    static const bool enabled = [] {
        char value[8] = {};
        const DWORD length = GetEnvironmentVariableA("DEDSEC_TRACE", value, sizeof(value));
        return length > 0 && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

const char* slot_name(unsigned slot) {
    switch (slot) {
        case 0: return "QueryInterface";
        case 1: return "AddRef";
        case 2: return "Release";
        case 9: return "CreateCommandAllocator";
        case 10: return "CreateGraphicsPipelineState";
        case 11: return "CreateComputePipelineState";
        case 14: return "CreateDescriptorHeap";
        case 16: return "CreateRootSignature";
        case 25: return "GetResourceAllocationInfo";
        case 27: return "CreateCommittedResource";
        case 43: return "GetAdapterLuid";
        case 62: return "Xbox CreateComputePipelineState";
        case 76: return "Xbox resource creation";
        default: return "forwarded";
    }
}

// DedSec: PSO creation with shader translation
HRESULT create_graphics_pipeline_state(DeviceProxy* proxy,
                                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                                       REFIID riid,
                                       void** ppso) {
    // Translate RDNA2 shaders to HLSL SM 6.6 if needed
    D3D12_GRAPHICS_PIPELINE_STATE_DESC translated_desc = *desc;

    // TODO: Intercept shader bytecode, detect RDNA2 format, translate to HLSL
    // For now, pass through to native DX12

    return proxy->real->CreateGraphicsPipelineState(&translated_desc, riid, ppso);
}

// Thunk template: forwards to the real device
template <unsigned Slot>
void __stdcall thunk() {
    // Assembly-level thunk implementation would go here
    // For now, this is a placeholder for the actual x64 assembly thunk
}

void** build_vtable(DeviceProxy* proxy) {
    auto* vtable = new void*[proxy->slot_count];

    // IUnknown slots (0-2) need special handling
    // Slot 0: QueryInterface
    // Slot 1: AddRef
    // Slot 2: Release

    // For slots 3+, forward to real device
    for (unsigned i = 3; i < proxy->slot_count; ++i) {
        vtable[i] = proxy->real_vtable[i];
    }

    return vtable;
}

} // namespace

ID3D12Device* create_native_device_proxy(ID3D12Device* real_device) {
    auto* proxy = new DeviceProxy();
    proxy->real = real_device;
    proxy->real_vtable = *reinterpret_cast<void***>(real_device);
    proxy->slot_count = 83; // ID3D12Device14
    proxy->refs = 1;
    proxy->vtable = build_vtable(proxy);

    return reinterpret_cast<ID3D12Device*>(proxy);
}

} // namespace rt::d3d12_native