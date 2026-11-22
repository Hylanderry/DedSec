// DedSec Fork — DX12 Native Pipeline
// Hi-Z Build Compute Shader — constructs min-depth mip pyramid
// Built from scratch for the DedSec DX12 native pipeline.
//
// Each dispatch builds one mip level by reading 2x2 blocks from the previous
// mip and writing the minimum depth value. This creates a conservative
// min-depth hierarchy used for occlusion culling.
//
// Input: Previous mip level (SRV t0) or raw depth buffer for mip 0
// Output: Current mip level (UAV u0)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_HIZ_THREAD_GROUP_X 8
#define DEDSEC_HIZ_THREAD_GROUP_Y 8

cbuffer HizConstants : register(b0) {
    uint2 input_size;     // Dimensions of the source mip
    uint2 output_size;    // Dimensions of the destination mip
    float2 inv_output_size;
    uint is_first_pass;   // 1 = reading raw depth buffer, 0 = reading previous mip
    uint padding;
};

Texture2D<float> input_depth : register(t0);
RWTexture2D<float> output_hiz : register(u0);

[numthreads(DEDSEC_HIZ_THREAD_GROUP_X, DEDSEC_HIZ_THREAD_GROUP_Y, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= output_size.x || id.y >= output_size.y)
        return;

    // Sample 2x2 block from input at corresponding position
    uint2 src_pos = id.xy * 2;

    float d00 = input_depth.Load(int3(min(src_pos + uint2(0, 0), input_size - 1), 0));
    float d10 = input_depth.Load(int3(min(src_pos + uint2(1, 0), input_size - 1), 0));
    float d01 = input_depth.Load(int3(min(src_pos + uint2(0, 1), input_size - 1), 0));
    float d11 = input_depth.Load(int3(min(src_pos + uint2(1, 1), input_size - 1), 0));

    // Conservative min-depth: take the NEAREST (minimum) depth in the 2x2 block
    // This ensures we never reject an object that might actually be visible
    float min_depth = min(min(d00, d10), min(d01, d11));

    output_hiz[id.xy] = min_depth;
}