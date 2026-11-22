// DedSec Fork — DX12 Native Pipeline
// Hi-Z Occlusion Culling Compute Shader
// Built from scratch for the DedSec DX12 native pipeline.
//
// Tests object AABBs against the hierarchical Z-buffer to determine visibility.
// Each thread processes one object: transforms its bounding box to clip space,
// selects the appropriate Hi-Z mip based on screen-space size, samples min-depth,
// and compares against the object's near-plane depth.
//
// Input:  Hi-Z texture (SRV t0), bounds buffer (SRV t1)
// Output: Cull results buffer (UAV u0)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_CULL_THREAD_GROUP_SIZE 64

struct CullBounds {
    float3 min;
    float3 max;
};

struct CullResult {
    uint visible;
    uint instance_count;
};

cbuffer CullConstants : register(b0) {
    float4x4 view_projection;
    uint object_count;
    uint hiz_mip_count;
    float2 hiz_size; // base mip dimensions
    uint padding[2];
};

Texture2D<float> hiz_texture : register(t0);
StructuredBuffer<CullBounds> bounds_buffer : register(t1);
RWStructuredBuffer<CullResult> results_buffer : register(u0);

// Project an AABB to screen-space and return the bounding rect + near depth
void project_aabb(float3 aabb_min, float3 aabb_max, out float4 screen_rect, out float near_depth) {
    // Transform all 8 corners of the AABB to clip space
    float4 corners[8];
    corners[0] = mul(float4(aabb_min.x, aabb_min.y, aabb_min.z, 1.0f), view_projection);
    corners[1] = mul(float4(aabb_max.x, aabb_min.y, aabb_min.z, 1.0f), view_projection);
    corners[2] = mul(float4(aabb_min.x, aabb_max.y, aabb_min.z, 1.0f), view_projection);
    corners[3] = mul(float4(aabb_max.x, aabb_max.y, aabb_min.z, 1.0f), view_projection);
    corners[4] = mul(float4(aabb_min.x, aabb_min.y, aabb_max.z, 1.0f), view_projection);
    corners[5] = mul(float4(aabb_max.x, aabb_min.y, aabb_max.z, 1.0f), view_projection);
    corners[6] = mul(float4(aabb_min.x, aabb_max.y, aabb_max.z, 1.0f), view_projection);
    corners[7] = mul(float4(aabb_max.x, aabb_max.y, aabb_max.z, 1.0f), view_projection);

    // Find min/max in NDC space (perspective divide)
    float ndc_min_x = 1e10f, ndc_min_y = 1e10f, ndc_max_x = -1e10f, ndc_max_y = -1e10f;
    near_depth = 1e10f;

    for (int i = 0; i < 8; i++) {
        float w = max(corners[i].w, 1e-6f);
        float ndc_x = corners[i].x / w;
        float ndc_y = corners[i].y / w;
        float ndc_z = corners[i].z / w;

        ndc_min_x = min(ndc_min_x, ndc_x);
        ndc_min_y = min(ndc_min_y, ndc_y);
        ndc_max_x = max(ndc_max_x, ndc_x);
        ndc_max_y = max(ndc_max_y, ndc_y);
        near_depth = min(near_depth, ndc_z);
    }

    // Convert NDC [-1,1] to UV [0,1]
    screen_rect = float4(
        ndc_min_x * 0.5f + 0.5f,
        ndc_min_y * 0.5f + 0.5f,
        ndc_max_x * 0.5f + 0.5f,
        ndc_max_y * 0.5f + 0.5f
    );
}

// Select the appropriate Hi-Z mip level based on screen-space bounding box size
uint select_hiz_mip(float4 screen_rect) {
    float width = (screen_rect.z - screen_rect.x) * hiz_size.x;
    float height = (screen_rect.w - screen_rect.y) * hiz_size.y;
    float max_dim = max(width, height);

    // Choose mip where the bounding box covers approximately 1-4 texels
    // Higher mips = more conservative (more false positives, fewer false negatives)
    uint mip = 0;
    float dim = max_dim;
    while (dim > 4.0f && mip < hiz_mip_count - 1) {
        dim *= 0.5f;
        mip++;
    }

    return mip;
}

[numthreads(DEDSEC_CULL_THREAD_GROUP_SIZE, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= object_count) return;

    CullBounds bounds = bounds_buffer[id.x];

    // Quick frustum cull: skip objects entirely behind camera
    // (simplified — full frustum test would check all 6 planes)
    float4 center_clip = mul(float4((bounds.min + bounds.max) * 0.5f, 1.0f), view_projection);
    if (center_clip.w < 0.1f) {
        // Behind near plane — cull
        results_buffer[id.x] = (CullResult){ 0, 0 };
        return;
    }

    // Project AABB to screen space
    float4 screen_rect;
    float near_depth;
    project_aabb(bounds.min, bounds.max, screen_rect, near_depth);

    // Clamp to valid UV range
    screen_rect.xy = max(screen_rect.xy, 0.0f);
    screen_rect.zw = min(screen_rect.zw, 1.0f);

    // Skip degenerate rects
    if (screen_rect.z <= screen_rect.x || screen_rect.w <= screen_rect.y) {
        results_buffer[id.x] = (CullResult){ 0, 0 };
        return;
    }

    // Select Hi-Z mip and sample min-depth at bounding box center
    uint mip = select_hiz_mip(screen_rect);
    float2 sample_uv = (screen_rect.xy + screen_rect.zw) * 0.5f;

    float min_depth = hiz_texture.SampleLevel(sampler_clamp, sample_uv, mip);

    // Conservative test: object is visible if its near depth <= min depth in Hi-Z
    // (Hi-Z stores minimum depth, so if object is closer than everything in that region, it's visible)
    uint visible = (near_depth <= min_depth + 1e-5f) ? 1 : 0;

    results_buffer[id.x] = (CullResult){ visible, visible };
}