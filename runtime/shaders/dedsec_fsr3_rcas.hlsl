// DedSec Fork — DX12 Native Pipeline
// FSR 3.1 RCAS (Robust Contrast Adaptive Sharpening) Compute Shader
// Built from scratch for the DedSec DX12 native pipeline.
//
// Post-upscale sharpening pass that enhances local contrast without
// introducing ringing or halo artifacts. Applied after EASU or temporal
// upscaling as a final image quality improvement step.
//
// Input: Upscaled color buffer (SRV)
// Output: Sharpened color buffer (UAV)

#define DEDSEC_FSR3_RCAS_THREAD_GROUP_X 16
#define DEDSEC_FSR3_RCAS_THREAD_GROUP_Y 16

cbuffer RcasConstants : register(b0) {
    uint2 output_size;
    float sharpness;      // 0.0 = no sharpening, 1.0 = maximum
    float denoise_factor; // 0.0 = full sharpen, 1.0 = suppress noise amplification
};

Texture2D<float4> input_color : register(t0);
RWTexture2D<float4> output_color : register(u0);

float luminance(float3 c) {
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

[numthreads(DEDSEC_FSR3_RCAS_THREAD_GROUP_X, DEDSEC_FSR3_RCAS_THREAD_GROUP_Y, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= output_size.x || id.y >= output_size.y)
        return;

    int2 pos = int2(id.xy);
    int2 dims = int2(output_size);

    // 3x3 neighborhood sampling
    float3 center = input_color.Load(int3(pos, 0)).rgb;
    float3 top    = input_color.Load(int3(clamp(pos + int2(0,-1), 0, dims-1), 0)).rgb;
    float3 bottom = input_color.Load(int3(clamp(pos + int2(0, 1), 0, dims-1), 0)).rgb;
    float3 left   = input_color.Load(int3(clamp(pos + int2(-1,0), 0, dims-1), 0)).rgb;
    float3 right  = input_color.Load(int3(clamp(pos + int2( 1,0), 0, dims-1), 0)).rgb;

    // Diagonal neighbors for better edge detection
    float3 tl = input_color.Load(int3(clamp(pos + int2(-1,-1), 0, dims-1), 0)).rgb;
    float3 tr = input_color.Load(int3(clamp(pos + int2( 1,-1), 0, dims-1), 0)).rgb;
    float3 bl = input_color.Load(int3(clamp(pos + int2(-1, 1), 0, dims-1), 0)).rgb;
    float3 br = input_color.Load(int3(clamp(pos + int2( 1, 1), 0, dims-1), 0)).rgb;

    // Compute local contrast using cross pattern
    float l_center = luminance(center);
    float l_cross = (luminance(top) + luminance(bottom) + luminance(left) + luminance(right)) * 0.25f;
    float l_diag  = (luminance(tl) + luminance(tr) + luminance(bl) + luminance(br)) * 0.25f;

    // Contrast-adaptive weight: less sharpening in high-contrast areas to avoid halos
    float local_contrast = abs(l_center - l_cross);
    float adaptive_weight = saturate(1.0f - local_contrast * 4.0f);

    // Noise suppression: reduce sharpening in low-contrast (potentially noisy) regions
    float noise_suppress = lerp(1.0f, adaptive_weight, denoise_factor);

    // Unsharp mask with adaptive strength
    float3 sharpened = center + (center - lerp(l_cross, l_diag, 0.5f).xxx)
                       * sharpness * adaptive_weight * noise_suppress * 2.0f;

    // Clamp to valid range to prevent overshooting
    float3 min_neighbor = min(min(top, bottom), min(left, right));
    float3 max_neighbor = max(max(top, bottom), max(left, right));
    sharpened = clamp(sharpened, min_neighbor - abs(center - min_neighbor) * 0.5f,
                                max_neighbor + abs(max_neighbor - center) * 0.5f);

    output_color[id.xy] = float4(saturate(sharpened), 1.0f);
}