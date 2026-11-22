// DedSec Fork — DX12 Native Pipeline
// GPU Tier Detection & Scalable Preset System — Implementation
// Built from scratch for the DedSec DX12 native pipeline.
//
// Detects GPU capabilities via DXGI adapter queries and D3D12 feature checks,
// then maps to a tier (POTATO→ULTRA) with an optimal quality preset.
// Every setting is overridable at runtime by the user.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_GPU_TIER_IMPLEMENTATION
#include "dedsec_gpu_tier.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace dedsec {

// ─── VRAM thresholds per tier (bytes) ─────────────────────────────────────────
static constexpr uint64_t kVramPotato  = 4ULL * 1024 * 1024 * 1024;   // ≤4 GB
static constexpr uint64_t kVramLow     = 6ULL * 1024 * 1024 * 1024;   // ≤6 GB
static constexpr uint64_t kVramMedium  = 8ULL * 1024 * 1024 * 1024;   // ≤8 GB
static constexpr uint64_t kVramHigh    = 12ULL * 1024 * 1024 * 1024;  // ≤12 GB
// >12 GB → ULTRA

// ─── Vendor detection from adapter description ────────────────────────────────
static GpuVendor detect_vendor(const std::string& name) {
    // Case-insensitive substring search
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("nvidia") != std::string::npos || lower.find("geforce") != std::string::npos)
        return GpuVendor::NVIDIA;
    if (lower.find("amd") != std::string::npos || lower.find("radeon") != std::string::npos)
        return GpuVendor::AMD;
    if (lower.find("intel") != std::string::npos || lower.find("arc") != std::string::npos)
        return GpuVendor::INTEL;
    return GpuVendor::UNKNOWN;
}

// ─── Feature detection via D3D12 CheckFeatureSupport ──────────────────────────
static GpuFeatures detect_features(ID3D12Device* device) {
    GpuFeatures f{};
    if (!device) return f;

    // Shader Model
    D3D12_FEATURE_DATA_SHADER_MODEL sm{};
    sm.HighestShaderModel = D3D_SHADER_MODEL_6_8;
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) {
        f.shader_model_6_6 = sm.HighestShaderModel >= D3D_SHADER_MODEL_6_6;
        f.shader_model_6_5 = sm.HighestShaderModel >= D3D_SHADER_MODEL_6_5;
    }

    // Variable Rate Shading (Tier 2 = full support)
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 opts6{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &opts6, sizeof(opts6)))) {
        f.variable_rate_shading = opts6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
    }

    // Mesh Shaders (SM 6.5+)
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7)))) {
        f.mesh_shaders = opts7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
    }

    // Ray Tracing
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5)))) {
        f.raytracing_tier_1_1 = opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
    }

    // Sampler Feedback (for texture streaming) — check via OPTIONS7 which has SamplerFeedbackTier
    // in all SDK versions that support the feature. Fall back to false if unavailable.
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7_sf{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7_sf, sizeof(opts7_sf)))) {
            f.sampler_feedback = opts7_sf.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9;
        }
    }

    // Enhanced Barriers
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 opts12{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &opts12, sizeof(opts12)))) {
        f.enhanced_barriers = opts12.EnhancedBarriersSupported;
    }

    return f;
}

// ─── Tier assignment logic ────────────────────────────────────────────────────
static GpuTier assign_tier(uint64_t vram_bytes, const GpuFeatures& features) {
    // Primary factor: VRAM + shader model
    // Secondary: specific feature gates

    if (vram_bytes >= 12ULL * 1024 * 1024 * 1024 && features.shader_model_6_6 && features.mesh_shaders)
        return GpuTier::ULTRA;

    if (vram_bytes >= 8ULL * 1024 * 1024 * 1024 && features.shader_model_6_5 && features.raytracing_tier_1_1)
        return GpuTier::HIGH;

    if (vram_bytes >= 6ULL * 1024 * 1024 * 1024 && features.shader_model_6_5 && features.variable_rate_shading)
        return GpuTier::MEDIUM;

    if (vram_bytes >= 6ULL * 1024 * 1024 * 1024)
        return GpuTier::LOW;

    return GpuTier::POTATO;
}

// ─── Public API ───────────────────────────────────────────────────────────────

GpuInfo GpuTierDetector::detect(ID3D12Device* device, IDXGIAdapter* adapter) {
    GpuInfo info{};

    if (adapter) {
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc))) {
            // Convert wide string to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string name(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, &name[0], len, nullptr, nullptr);
                info.adapter_name = name;
            }
            info.vram_bytes = static_cast<uint64_t>(desc.DedicatedVideoMemory);
            info.vendor = detect_vendor(info.adapter_name);
        }
    }

    info.features = detect_features(device);
    info.tier = assign_tier(info.vram_bytes, info.features);

    // VRAM budget: leave 512MB headroom for system/OS
    info.vram_budget_bytes = info.vram_bytes > (512ULL * 1024 * 1024)
        ? info.vram_bytes - (512ULL * 1024 * 1024)
        : info.vram_bytes;

    return info;
}

QualityPreset GpuTierDetector::get_preset_for_tier(GpuTier tier, uint32_t display_w, uint32_t display_h) {
    QualityPreset p{};

    switch (tier) {
        case GpuTier::POTATO:
            p.render_scale      = 0.5f;   // 360p internal → 720p via FSR
            p.texture_quality   = 0;
            p.shadow_resolution = 256;
            p.shadow_cascades   = 1;
            p.shadow_pcf        = false;
            p.ssr_enabled       = false;
            p.ssao_enabled      = false;
            p.volumetric_fog    = false;
            p.screen_space_gi   = false;
            p.draw_distance     = 0.3f;
            p.lod_bias          = -2;
            p.vrs_tier          = 2;      // 2x2 coarse VRS if supported
            p.async_compute     = true;
            p.shader_cache      = true;
            p.fsr_upscaling     = true;
            p.fsr_frame_gen     = true;   // Critical for potato GPUs
            p.fsr_sharpness     = 0.6f;
            p.vram_limit_mb     = 3500;
            break;

        case GpuTier::LOW:
            p.render_scale      = 0.67f;  // ~480p → 720p or 720p → 1080p
            p.texture_quality   = 1;
            p.shadow_resolution = 512;
            p.shadow_cascades   = 2;
            p.shadow_pcf        = false;
            p.ssr_enabled       = false;
            p.ssao_enabled      = true;
            p.volumetric_fog    = false;
            p.screen_space_gi   = false;
            p.draw_distance     = 0.5f;
            p.lod_bias          = -1;
            p.vrs_tier          = 1;
            p.async_compute     = true;
            p.shader_cache      = true;
            p.fsr_upscaling     = true;
            p.fsr_frame_gen     = true;
            p.fsr_sharpness     = 0.5f;
            p.vram_limit_mb     = 5500;
            break;

        case GpuTier::MEDIUM:
            p.render_scale      = 0.75f;  // 1080p → 1440p or 720p → 1080p
            p.texture_quality   = 2;
            p.shadow_resolution = 1024;
            p.shadow_cascades   = 3;
            p.shadow_pcf        = true;
            p.ssr_enabled       = true;
            p.ssao_enabled      = true;
            p.volumetric_fog    = false;
            p.screen_space_gi   = false;
            p.draw_distance     = 0.75f;
            p.lod_bias          = 0;
            p.vrs_tier          = 1;
            p.async_compute     = true;
            p.shader_cache      = true;
            p.fsr_upscaling     = true;
            p.fsr_frame_gen     = false;  // Not needed at this tier typically
            p.fsr_sharpness     = 0.4f;
            p.vram_limit_mb     = 7500;
            break;

        case GpuTier::HIGH:
            p.render_scale      = 1.0f;   // Native resolution
            p.texture_quality   = 3;
            p.shadow_resolution = 2048;
            p.shadow_cascades   = 4;
            p.shadow_pcf        = true;
            p.ssr_enabled       = true;
            p.ssao_enabled      = true;
            p.volumetric_fog    = true;
            p.screen_space_gi   = true;
            p.draw_distance     = 1.0f;
            p.lod_bias          = 0;
            p.vrs_tier          = 0;      // Off — not needed
            p.async_compute     = true;
            p.shader_cache      = true;
            p.fsr_upscaling     = false;  // Native res, no upscale needed
            p.fsr_frame_gen     = false;
            p.fsr_sharpness     = 0.3f;
            p.vram_limit_mb     = 11000;
            break;

        case GpuTier::ULTRA:
        default:
            p.render_scale      = 1.0f;
            p.texture_quality   = 3;
            p.shadow_resolution = 4096;
            p.shadow_cascades   = 4;
            p.shadow_pcf        = true;
            p.ssr_enabled       = true;
            p.ssao_enabled      = true;
            p.volumetric_fog    = true;
            p.screen_space_gi   = true;
            p.draw_distance     = 1.0f;
            p.lod_bias          = 0;
            p.vrs_tier          = 0;
            p.async_compute     = true;
            p.shader_cache      = true;
            p.fsr_upscaling     = false;
            p.fsr_frame_gen     = false;
            p.fsr_sharpness     = 0.2f;
            p.vram_limit_mb     = 0;      // Auto
            break;
    }

    return p;
}

void GpuTierDetector::apply_overrides(QualityPreset& preset, const std::string& overrides_ini) {
    // Simple key=value parser for INI-style overrides
    // Format: key=value\nkey=value\n...
    // Supported keys match QualityPreset field names

    size_t pos = 0;
    while (pos < overrides_ini.size()) {
        size_t line_end = overrides_ini.find('\n', pos);
        if (line_end == std::string::npos) line_end = overrides_ini.size();

        std::string line = overrides_ini.substr(pos, line_end - pos);
        pos = line_end + 1;

        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            size_t start = s.find_first_not_of(" \t\r");
            size_t end = s.find_last_not_of(" \t\r");
            if (start == std::string::npos) { s.clear(); return; }
            s = s.substr(start, end - start + 1);
        };
        trim(key);
        trim(val);

        // Map keys to preset fields
        if (key == "render_scale")       preset.render_scale = std::stof(val);
        else if (key == "texture_quality") preset.texture_quality = static_cast<uint8_t>(std::stoi(val));
        else if (key == "shadow_resolution") preset.shadow_resolution = static_cast<uint32_t>(std::stoi(val));
        else if (key == "shadow_cascades") preset.shadow_cascades = static_cast<uint8_t>(std::stoi(val));
        else if (key == "shadow_pcf")    preset.shadow_pcf = (val == "1" || val == "true");
        else if (key == "ssr")           preset.ssr_enabled = (val == "1" || val == "true");
        else if (key == "ssao")          preset.ssao_enabled = (val == "1" || val == "true");
        else if (key == "volumetric_fog") preset.volumetric_fog = (val == "1" || val == "true");
        else if (key == "draw_distance") preset.draw_distance = std::stof(val);
        else if (key == "lod_bias")      preset.lod_bias = static_cast<int8_t>(std::stoi(val));
        else if (key == "vrs_tier")      preset.vrs_tier = static_cast<uint8_t>(std::stoi(val));
        else if (key == "async_compute") preset.async_compute = (val == "1" || val == "true");
        else if (key == "fsr_upscaling") preset.fsr_upscaling = (val == "1" || val == "true");
        else if (key == "fsr_frame_gen") preset.fsr_frame_gen = (val == "1" || val == "true");
        else if (key == "fsr_sharpness") preset.fsr_sharpness = std::stof(val);
        else if (key == "vram_limit_mb") preset.vram_limit_mb = static_cast<uint64_t>(std::stoull(val));
    }
}

const char* GpuTierDetector::tier_name(GpuTier tier) {
    switch (tier) {
        case GpuTier::POTATO: return "POTATO";
        case GpuTier::LOW:    return "LOW";
        case GpuTier::MEDIUM: return "MEDIUM";
        case GpuTier::HIGH:   return "HIGH";
        case GpuTier::ULTRA:  return "ULTRA";
        default:              return "UNKNOWN";
    }
}

const char* GpuTierDetector::vendor_name(GpuVendor vendor) {
    switch (vendor) {
        case GpuVendor::NVIDIA: return "NVIDIA";
        case GpuVendor::AMD:    return "AMD";
        case GpuVendor::INTEL:  return "Intel";
        default:                return "Unknown";
    }
}

void GpuTierDetector::log_gpu_info(const GpuInfo& info) {
    std::printf("[DedSec GPU] ═══════════════════════════════════════\n");
    std::printf("[DedSec GPU] Adapter: %s\n", info.adapter_name.c_str());
    std::printf("[DedSec GPU] Vendor:  %s\n", vendor_name(info.vendor));
    std::printf("[DedSec GPU] VRAM:    %.1f GB (%.1f GB available)\n",
                info.vram_bytes / (1024.0 * 1024 * 1024),
                info.vram_budget_bytes / (1024.0 * 1024 * 1024));
    std::printf("[DedSec GPU] Tier:    %s\n", tier_name(info.tier));
    std::printf("[DedSec GPU] SM 6.6:  %s | SM 6.5: %s\n",
                info.features.shader_model_6_6 ? "YES" : "no",
                info.features.shader_model_6_5 ? "YES" : "no");
    std::printf("[DedSec GPU] VRS:     %s | Mesh: %s | RT: %s\n",
                info.features.variable_rate_shading ? "YES" : "no",
                info.features.mesh_shaders ? "YES" : "no",
                info.features.raytracing_tier_1_1 ? "YES" : "no");
    std::printf("[DedSec GPU] SamplerFeedback: %s | EnhancedBarriers: %s\n",
                info.features.sampler_feedback ? "YES" : "no",
                info.features.enhanced_barriers ? "YES" : "no");
    std::printf("[DedSec GPU] ═══════════════════════════════════════\n");
}

} // namespace dedsec