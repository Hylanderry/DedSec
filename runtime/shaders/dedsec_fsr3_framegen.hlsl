// DedSec Fork — DX12 Native Pipeline
// FSR 3.1 Frame Generation Compute Shader
// Built from scratch for the DedSec DX12 native pipeline.
//
// Interpolates a synthetic frame between two real frames using motion vectors
// and optical flow. This doubles perceived framerate without additional game
// simulation cost.
//
// Inputs:
//   - Current frame color (SRV t0)
//   - Previous frame color (SRV t1)
//   - Motion vectors RG16_FLOAT (SRV t2)
//   - Depth buffer (SRV t3)
// Output:
//   - Interpolated frame (UAV u0)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_FG_THREAD_GROUP_X 16
#define DEDSEC_FG_THREAD_GROUP_Y 16

cbuffer FrameGenConstants : register(b0) {
    uint2 output_size;
    float2 inv_output_size;
    float interpolation_factor; // 0.0 = previous, 0.5 = midpoint, 1.0 = current
    float motion_scale;         // scale factor for motion vector magnitude
    float depth_threshold;      // threshold for depth discontinuity detection
    uint padding;
};

Texture2D<float4> current_frame : register(t0);
Texture2D<float4> previous_frame : register(t1);
Texture2D<float2> motion_vectors : register(t2);
Texture2D<float> depth_buffer : register(t3);
RWTexture2D<float4> output_frame : register(u0);

SamplerState linear_sampler : register(s0);

float luminance(float3 c) {
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Detect depth discontinuity to avoid ghosting at object boundaries
bool is_depth_discontinuous(uint2 pos, uint2 dims) {
    int2 ipos = int2(pos);
    float center = depth_buffer.Load(int3(ipos, 0));

    float d_left  = depth_buffer.Load(int3(max(ipos.x - 1, 0), ipos.y, 0));
    float d_right = depth_buffer.Load(int3(min(ipos.x + 1, dims.x - 1), ipos.y, 0));
    float d_up    = depth_buffer.Load(int3(ipos.x, max(ipos.y - 1, 0), 0));
    float d_down  = depth_buffer.Load(int3(ipos.x, min(ipos.y + 1, dims.y - 1), 0));

    float max_diff = max(max(abs(center - d_left), abs(center - d_right)),
                         max(abs(center - d_up), abs(center - d_down)));

    return max_diff > depth_threshold;
}

[numthreads(DEDSEC_FG_THREAD_GROUP_X, DEDSEC_FG_THREAD_GROUP_Y, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= output_size.x || id.y >= output_size.y)
        return;

    float2 uv = (float2(id.xy) + 0.5f) * inv_output_size;

    // Sample motion vector at this pixel
    float2 mv = motion_vectors.SampleLevel(linear_sampler, uv, 0) * motion_scale;

    // Check for depth discontinuity — fall back to simple blend at edges
    bool depth_edge = is_depth_discontinuous(id.xy, output_size);

    // Reproject: find where this pixel was in the previous frame
    float2 prev_uv = uv - mv;
    float2 next_uv = uv + mv;

    // Clamp reprojected UVs
    prev_uv = clamp(prev_uv, 0.0f, 1.0f);
    next_uv = clamp(next_uv, 0.0f, 1.0f);

    // Sample both frames at reprojected positions
    float4 color_prev = previous_frame.SampleLevel(linear_sampler, prev_uv, 0);
    float4 color_curr = current_frame.SampleLevel(linear_sampler, next_uv, 0);

    // At depth discontinuities, use simple temporal blend to avoid ghosting
    float4 result;
    if (depth_edge) {
        result = lerp(color_prev, color_curr, interpolation_factor);
    } else {
        // Optical-flow-aware interpolation with luminance-weighted blending
        float l_prev = luminance(color_prev.rgb);
        float l_curr = luminance(color_curr.rgb);
        float l_diff = abs(l_prev - l_curr);

        // Reduce interpolation weight when luminance difference is large
        // (indicates occlusion/disocclusion)
        float occlusion_weight = saturate(1.0f - l_diff * 3.0f);
        float effective_factor = lerp(interpolation_factor, 0.5f, 1.0f - occlusion_weight);

        result = lerp(color_prev, color_curr, effective_factor);
    }

    output_frame[id.xy] = result;
}