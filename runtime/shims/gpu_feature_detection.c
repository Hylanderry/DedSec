// gpu_feature_detection.c
//
// Detecção real de capabilities GPU via D3D12 CheckFeatureSupport.
// O DedSec atualmente retorna XboxScarlettAnaconda hardcoded via env var.
// Esse código faz tiering real por feature level, RT tier, VRS, mesh shader, etc.
//
// Integrar no runtime como alternativa ao DEDSEC_DEVICE_TYPE manual.

#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>

typedef struct {
    int vendor_id;
    int device_id;
    UINT64 vram_bytes;
    D3D_FEATURE_LEVEL feature_level;
    D3D12_RAYTRACING_TIER rt_tier;
    D3D12_VARIABLE_SHADING_RATE_TIER vrs_tier;
    BOOL mesh_shader_support;
    BOOL sampler_feedback_support;
    int subgroup_size; // 32 ou 64
    const char* backend_name; // "RDNA", "Turing", "Ampere", etc
} gpu_caps_t;

int detect_gpu(ID3D12Device* device, gpu_caps_t* out) {
    if (!device || !out) return -1;
    memset(out, 0, sizeof(*out));

    // Adapter info via DXGI
    IDXGIFactory4* factory = NULL;
    CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&factory);
    if (!factory) return -1;

    IDXGIAdapter1* adapter = NULL;
    factory->lpVtbl->EnumAdapters1(factory, 0, &adapter);
    factory->lpVtbl->Release(factory);
    if (!adapter) return -1;

    DXGI_ADAPTER_DESC1 desc;
    adapter->lpVtbl->GetDesc1(adapter, &desc);
    out->vendor_id = desc.VendorId;
    out->device_id = desc.DeviceId;
    out->vram_bytes = (UINT64)desc.DedicatedVideoMemory;
    adapter->lpVtbl->Release(adapter);

    // Feature level check
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl = {0};
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1
    };
    fl.NumFeatureLevels = 4;
    fl.pFeatureLevelsRequested = levels;
    device->lpVtbl->CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl));
    out->feature_level = fl.MaxSupportedFeatureLevel;

    // Ray tracing
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opt5 = {0};
    if (SUCCEEDED(device->lpVtbl->CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5)))) {
        out->rt_tier = opt5.RaytracingTier;
    }

    // VRS
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 opt6 = {0};
    if (SUCCEEDED(device->lpVtbl->CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS6, &opt6, sizeof(opt6)))) {
        out->vrs_tier = opt6.VariableShadingRateTier;
    }

    // Mesh shaders + sampler feedback
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 opt7 = {0};
    if (SUCCEEDED(device->lpVtbl->CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7)))) {
        out->mesh_shader_support = (opt7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED);
        out->sampler_feedback_support = (opt7.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED);
    }

    // Subgroup size (D3D12_OPTIONS1)
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 opt1 = {0};
    if (SUCCEEDED(device->lpVtbl->CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS1, &opt1, sizeof(opt1)))) {
        out->subgroup_size = (int)opt1.WaveLaneCountMin;
    }

    // Backend name heuristic
    if (out->vendor_id == 0x1002) out->backend_name = "AMD";
    else if (out->vendor_id == 0x10DE) out->backend_name = "NVIDIA";
    else if (out->vendor_id == 0x8086) out->backend_name = "Intel";
    else out->backend_name = "Unknown";

    return 0;
}

// Mapeia GPU real pro device type do DedSec
// 6 = Lockhart (Series S), 7 = Anaconda (Series X), 1 = PC
uint32_t gpu_to_dedsec_device_type(const gpu_caps_t* caps) {
    // Se tem RT tier 1.1+ e mesh shader = hardware moderno, reportar Anaconda
    if (caps->rt_tier >= D3D12_RAYTRACING_TIER_1_1 && caps->mesh_shader_support) {
        return 7; // Anaconda
    }
    // Se tem pelo menos feature level 12_0, reportar Lockhart
    if (caps->feature_level >= D3D_FEATURE_LEVEL_12_0) {
        return 6; // Lockhart
    }
    // Fallback: PC
    return 1;
}

// Exemplo de uso:
// gpu_caps_t caps;
// detect_gpu(device, &caps);
// uint32_t device_type = gpu_to_dedsec_device_type(&caps);
// fprintf(stderr, "[rt] GPU: %s vendor=0x%04X, VRAM=%lluMB, FL=%d, RT=%d, VRS=%d, mesh=%d, wave=%d\n",
//         caps.backend_name, caps.vendor_id, caps.vram_bytes / (1024*1024),
//         caps.feature_level, caps.rt_tier, caps.vrs_tier,
//         caps.mesh_shader_support, caps.subgroup_size);
// fprintf(stderr, "[rt] DedSec device type: %u\n", device_type);