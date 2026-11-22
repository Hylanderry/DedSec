// DedSec Fork — DX12 Native Pipeline
// GPU Tier Detection & Scalable Preset System
//
// Automatically detects GPU capabilities and applies optimal settings.
// Scales from "potato" (any DX12 GPU) to "ultra" (RTX 4090 / RX 7900 XTX).
//
// Tiers:
// - TIER_POTATO: GTX 1650, RX 570, integrated GPUs (4GB VRAM, SM 6.0-6.5)
// - TIER_LOW: GTX 1060, RX 580 (6GB VRAM, SM 6.0-6.5)
// - TIER_MEDIUM: RTX 2060, RX 6600 (6-8GB VRAM, SM 6.5+, VRS)
// - TIER_HIGH: RTX 3070, RX 6800 (8-12GB VRAM, SM 6.5+, RT cores)
// - TIER_ULTRA: RTX 4070+, RX 7800 XT+ (12GB+ VRAM, SM 6.6+, mesh shaders)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <string>

namespace dedsec {

enum class GpuTier : uint8_t {
    POTATO = 0,      // GTX 1650, RX 570, integrated (4GB, SM 6.0-6.5)
    LOW = 1,         // GTX 1060, RX 580 (6GB, SM 6.0-6.5)
    MEDIUM = 2,      // RTX 2060, RX 6600 (6-8GB, SM 6.5+, VRS)
    HIGH = 3,        // RTX 3070, RX 6800 (8-12GB, SM 6.5+, RT)
    ULTRA = 4,       // RTX 4070+, RX 7800 XT+ (12GB+, SM 6.6+, mesh)
};

enum class GpuVendor : uint8_t {
    UNKNOWN = 0,
    NVIDIA = 1,
    AMD = 2,
    INTEL = 3,
};

// Hardware feature support detection
struct GpuFeatures {
    bool shader_model_6_6 = false;
    bool shader_model_6_5 = false;
    bool variable_rate_shading = false;
    bool mesh_shaders = false;
    bool raytracing_tier_1_1 = false;
    bool sampler_feedback = false;
    bool enhanced_barriers = false;
    bool work_graphs = false;  // SM 6.8+
};

// Detected GPU info
struct GpuInfo {
    GpuTier tier = GpuTier::POTATO;
    GpuVendor vendor = GpuVendor::UNKNOWN;
    std::string adapter_name;
    uint64_t vram_bytes = 0;
    uint64_t vram_budget_bytes = 0;  // Available VRAM
    GpuFeatures features;
    uint32_t compute_units = 0;  // SMs (NVIDIA) or CUs (AMD)
};

// Quality preset that scales with GPU tier
struct QualityPreset {
    // Render scale (0.5 = 50% = half resolution per axis)
    float render_scale = 1.0f;

    // Texture quality (0 = lowest, 3 = ultra)
    uint8_t texture_quality = 0;

    // Shadow settings
    uint32_t shadow_resolution = 512;
    uint8_t shadow_cascades = 2;
    bool shadow_pcf = false;

    // Lighting & effects
    bool ssr_enabled = false;          // Screen-space reflections
    bool ssao_enabled = false;         // Screen-space ambient occlusion
    bool volumetric_fog = false;
    bool screen_space_gi = false;

    // Draw distance multiplier (0.3 = 30% of original)
    float draw_distance = 1.0f;

    // LOD bias (negative = more aggressive LODs)
    int8_t lod_bias = 0;

    // Variable Rate Shading tier (0 = off, 1 = 1x1, 2 = 2x2 coarse)
    uint8_t vrs_tier = 0;

    // Async compute (always beneficial when supported)
    bool async_compute = true;

    // Shader cache
    bool shader_cache = true;

    // FSR settings
    bool fsr_upscaling = true;
    bool fsr_frame_gen = false;
    float fsr_sharpness = 0.5f;

    // VRAM budget (leave headroom for system)
    uint64_t vram_limit_mb = 0;  // 0 = auto (VRAM - 512MB)
};

// GPU detection and preset system
class GpuTierDetector {
public:
    // Detect GPU capabilities from D3D12 device
    static GpuInfo detect(ID3D12Device* device, IDXGIAdapter* adapter);

    // Get recommended preset for detected GPU tier
    static QualityPreset get_preset_for_tier(GpuTier tier, uint32_t display_width, uint32_t display_height);

    // Apply manual override to preset (user changed settings)
    static void apply_overrides(QualityPreset& preset, const std::string& overrides_ini);

    // Get human-readable tier name
    static const char* tier_name(GpuTier tier);

    // Get vendor name
    static const char* vendor_name(GpuVendor vendor);

    // Log detected GPU info
    static void log_gpu_info(const GpuInfo& info);
};

// Helper: check if GPU supports a specific feature
inline bool supports_vrs(const GpuInfo& info) { return info.features.variable_rate_shading; }
inline bool supports_mesh_shaders(const GpuInfo& info) { return info.features.mesh_shaders; }
inline bool supports_raytracing(const GpuInfo& info) { return info.features.raytracing_tier_1_1; }
inline bool supports_sampler_feedback(const GpuInfo& info) { return info.features.sampler_feedback; }

} // namespace dedsec