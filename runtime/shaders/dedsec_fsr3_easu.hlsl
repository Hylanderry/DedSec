// DedSec Fork — DX12 Native Pipeline
// FSR 3.1 EASU (Edge Adaptive Spatial Upsampling) Compute Shader
// Built from scratch for the DedSec DX12 native pipeline.
//
// This shader performs edge-adaptive spatial upscaling from a lower render
// resolution to the display resolution. It analyzes local edge direction
// and applies anisotropic filtering along edges to preserve detail while
// smoothing jagged lines.
//
// Input: Low-resolution color buffer (SRV)
// Output: Upscaled color buffer (UAV)
//
// Based on AMD FidelityFX Super Resolution algorithm principles,
// implemented independently for the DedSec DX12 native pipeline.

#define DEDSEC_FSR3_EASU_THREAD_GROUP_X 16
#define DEDSEC_FSR3_EASU_THREAD_GROUP_Y 16

cbuffer EasuConstants : register(b0) {
    uint2 input_size;      // Render resolution (before upscale)
    uint2 output_size;     // Display resolution (after upscale)
    float2 scale_factor;   // output_size / input_size
    float2 inv_scale;      // input_size / output_size
    float sharpness;       // 0.0 = soft, 1.0 = sharp
    uint padding;
};

Texture2D<float4> input_color : register(t0);
RWTexture2D<float4> output_color : register(u0);

SamplerState linear_sampler : register(s0);

// Edge detection kernel weights
static const float kEdgeThreshold = 0.05f;
static const float kMaxAnisotropy = 4.0f;

// Compute luminance from RGB
float luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// Detect edge direction using Sobel-like gradient analysis
float2 detect_edge_direction(uint2 pos, uint2 input_dim) {
    // Sample neighboring pixels for gradient computation
    int2 ipos = int2(pos);

    float l_center = luminance(input_color.Load(int3(ipos, 0)).rgb);
    float l_left   = luminance(input_color.Load(int3(max(ipos.x - 1, 0), ipos.y, 0)).rgb);
    float l_right  = luminance(input_color.Load(int3(min(ipos.x + 1, input_dim.x - 1), ipos.y, 0)).rgb);
    float l_up     = luminance(input_color.Load(int3(ipos.x, max(ipos.y - 1, 0), 0)).rgb);
    float l_down   = luminance(input_color.Load(int3(ipos.x, min(ipos.y + 1, input_dim.y - 1), 0)).rgb);

    // Horizontal and vertical gradients
    float grad_h = l_right - l_left;
    float grad_v = l_down - l_up;

    // Normalize to get edge direction perpendicular to gradient
    float len = sqrt(grad_h * grad_h + grad_v * grad_v) + 1e-6f;
    return float2(-grad_v, grad_h) / len;
}

// Anisotropic bilinear filter along edge direction
float4 easu_filter(float2 uv, float2 edge_dir, float aniso_strength) {
    // Stretch sampling kernel along edge direction
    float2 offset_x = float2(1.0f, 0.0f);
    float2 offset_y = float2(0.0f, 1.0f);

    // Rotate offsets to align with edge
    float cos_a = edge_dir.x;
    float sin_a = edge_dir.y;

    float2 along_edge = float2(cos_a, sin_a) * aniso_strength;
    float2 across_edge = float2(-sin_a, cos_a);

    // Sample along edge direction for smoother interpolation
    float4 c0 = input_color.SampleLevel(linear_sampler, uv - along_edge * 0.5f, 0);
    float4 c1 = input_color.SampleLevel(linear_sampler, uv, 0);
    float4 c2 = input_color.SampleLevel(linear_sampler, uv + along_edge * 0.5f, 0);

    // Weighted average favoring center sample
    return (c0 + c1 * 2.0f + c2) / 4.0f;
}

[numthreads(DEDSEC_FSR3_EASU_THREAD_GROUP_X, DEDSEC_FSR3_EASU_THREAD_GROUP_Y, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID) {
    if (dispatch_id.x >= output_size.x || dispatch_id.y >= output_size.y)
        return;

    // Map output pixel to input UV space
    float2 uv = ((float2(dispatch_id.xy) + 0.5f) * inv_scale) / float2(input_size);

    // Clamp UV to valid range
    uv = clamp(uv, 0.5f / float2(input_size), 1.0f - 0.5f / float2(input_size));

    // Get corresponding input pixel position
    uint2 input_pos = uint2(uv * float2(input_size));
    input_pos = min(input_pos, input_size - 1);

    // Detect edge direction at this location
    float2 edge_dir = detect_edge_direction(input_pos, input_size);

    // Compute edge strength to determine anisotropy amount
    float l_center = luminance(input_color.Load(int3(int2(input_pos), 0)).rgb);
    float l_neighbor = luminance(input_color.Load(int3(
        min(int2(input_pos) + int2(edge_dir * 2.0f), int2(input_size) - 1), 0)).rgb);
    float edge_strength = abs(l_center - l_neighbor);

    // Scale anisotropy by edge strength and user sharpness preference
    float aniso = lerp(1.0f, kMaxAnisotropy, saturate(edge_strength / kEdgeThreshold))
                  * lerp(0.5f, 1.5f, sharpness);

    // Apply edge-adaptive upsampling
    float4 result = easu_filter(uv, edge_dir, aniso);

    // Write upscaled pixel
    output_color[dispatch_id.xy] = result;
}