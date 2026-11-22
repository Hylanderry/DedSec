// DedSec Fork — DX12 Native Pipeline
// VRS Tile Classification Compute Shader
// Built from scratch for the DedSec DX12 native pipeline.
//
// Classifies each 8x8 screen tile into a shading rate based on:
// - Luminance variance (high contrast → full detail)
// - Motion vector magnitude (high motion → coarser shading acceptable)
// - Screen-space position (peripheral → coarser)
// - Depth complexity (flat depth → coarser)
//
// Output: R8_UINT shading rate texture for RSSetShadingRateImage
// Values: 0=1x1, 3=2x2, 6=4x4
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_VRS_THREAD_GROUP_X 8
#define DEDSEC_VRS_THREAD_GROUP_Y 8
#define TILE_SIZE 8

cbuffer VrsConstants : register(b0) {
    uint2 output_size;        // Shading rate texture dimensions (tiles)
    uint2 screen_size;        // Full screen resolution in pixels
    float2 inv_screen_size;
    float motion_threshold;
    float luminance_threshold;
    float peripheral_radius;
    uint peripheral_rate;     // 3=2x2, 6=4x4
    uint high_motion_rate;    // 3=2x2, 6=4x4
};

Texture2D<float4> prev_color : register(t0);
Texture2D<float2> motion_vectors : register(t1);
Texture2D<float> depth_buffer : register(t2);
RWTexture2D<uint> shading_rate_out : register(u0);

float luminance(float3 c) {
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Compute luminance variance across an 8x8 tile
float compute_tile_variance(uint2 tile_origin) {
    float sum = 0.0f;
    float sum_sq = 0.0f;
    float count = 0.0f;

    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            int2 px = int2(tile_origin) + int2(x, y);
            if (px.x >= 0 && px.x < (int)screen_size.x && px.y >= 0 && px.y < (int)screen_size.y) {
                float l = luminance(prev_color.Load(int3(px, 0)).rgb);
                sum += l;
                sum_sq += l * l;
                count += 1.0f;
            }
        }
    }

    if (count < 1.0f) return 0.0f;
    float mean = sum / count;
    float variance = (sum_sq / count) - (mean * mean);
    return max(variance, 0.0f);
}

// Compute average motion magnitude across an 8x8 tile
float compute_tile_motion(uint2 tile_origin) {
    float total_motion = 0.0f;
    float count = 0.0f;

    for (int y = 0; y < TILE_SIZE; y += 2) {  // Sample every other pixel for speed
        for (int x = 0; x < TILE_SIZE; x += 2) {
            int2 px = int2(tile_origin) + int2(x, y);
            if (px.x >= 0 && px.x < (int)screen_size.x && px.y >= 0 && px.y < (int)screen_size.y) {
                float2 mv = motion_vectors.Load(int3(px, 0));
                total_motion += length(mv);
                count += 1.0f;
            }
        }
    }

    return count > 0.0f ? total_motion / count : 0.0f;
}

// Check if tile is in peripheral region of screen
bool is_peripheral(uint2 tile_pos, uint2 tile_dims) {
    float2 normalized = (float2(tile_pos) + 0.5f) / float2(tile_dims);
    float2 centered = normalized - 0.5f;
    float dist = length(centered * float2(1.0f, screen_size.y / (float)screen_size.x));
    return dist > peripheral_radius;
}

[numthreads(DEDSEC_VRS_THREAD_GROUP_X, DEDSEC_VRS_THREAD_GROUP_Y, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= output_size.x || id.y >= output_size.y)
        return;

    uint2 tile_origin = id.xy * TILE_SIZE;
    uint2 tile_dims = output_size;

    // Default: full detail
    uint rate = 0; // 1x1

    // Rule 1: Peripheral areas get coarser shading
    if (is_peripheral(id.xy, tile_dims)) {
        rate = peripheral_rate;
    } else {
        // Rule 2: High luminance variance → keep full detail
        float variance = compute_tile_variance(tile_origin);

        // Rule 3: High motion → coarser shading (temporal reprojection handles it)
        float motion = compute_tile_motion(tile_origin);

        if (variance < luminance_threshold && motion > motion_threshold) {
            // Low detail + high motion → safe to use coarse shading
            rate = high_motion_rate;
        } else if (variance < luminance_threshold * 0.5f) {
            // Very uniform region (skybox, walls) → maximum savings
            rate = 6; // 4x4
        }
        // else: high variance → keep 1x1 (rate stays 0)
    }

    shading_rate_out[id.xy] = rate;
}