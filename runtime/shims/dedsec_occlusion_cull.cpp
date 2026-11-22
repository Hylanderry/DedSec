// DedSec Fork — DX12 Native Pipeline
// Hi-Z Occlusion Culling — Implementation
// Built from scratch for the DedSec DX12 native pipeline.
//
// Builds a hierarchical Z-buffer (min-depth mip chain) from the main depth buffer,
// then tests object AABBs against it on the async compute queue to reject occluded
// draw calls before they reach the vertex/fragment stages.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_OCCLUSION_CULL_IMPLEMENTATION
#include "dedsec_occlusion_cull.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace dedsec {

// ─── Static state ─────────────────────────────────────────────────────────────
ID3D12Device* OcclusionCullSystem::s_device = nullptr;
ID3D12Resource* OcclusionCullSystem::s_hiz_texture = nullptr;
ID3D12Resource* OcclusionCullSystem::s_bounds_buffer = nullptr;
ID3D12Resource* OcclusionCullSystem::s_results_buffer = nullptr;
ID3D12Resource* OcclusionCullSystem::s_upload_buffer = nullptr;
ID3D12PipelineState* OcclusionCullSystem::s_hiz_pso = nullptr;
ID3D12PipelineState* OcclusionCullSystem::s_cull_pso = nullptr;
ID3D12RootSignature* OcclusionCullSystem::s_root_sig = nullptr;
uint32_t OcclusionCullSystem::s_max_objects = 0;
uint32_t OcclusionCullSystem::s_last_visible = 0;
uint32_t OcclusionCullSystem::s_last_culled = 0;
bool OcclusionCullSystem::s_initialized = false;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static uint32_t next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

static uint32_t count_mips(uint32_t w, uint32_t h) {
    uint32_t max_dim = std::max(w, h);
    uint32_t mips = 1;
    while (max_dim > 1) { max_dim >>= 1; mips++; }
    return mips;
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool OcclusionCullSystem::initialize(ID3D12Device* device, uint32_t max_objects) {
    if (s_initialized) return true;
    if (!device || max_objects == 0) return false;

    s_device = device;
    s_device->AddRef();
    s_max_objects = max_objects;

    // Create bounds buffer (StructuredBuffer<CullBounds>)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(max_objects) * sizeof(CullBounds);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = s_device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&s_bounds_buffer));
        if (FAILED(hr)) { shutdown(); return false; }
    }

    // Create upload buffer for bounds staging
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(max_objects) * sizeof(CullBounds);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        HRESULT hr = s_device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_upload_buffer));
        if (FAILED(hr)) { shutdown(); return false; }
    }

    // Create results buffer (RWStructuredBuffer<CullResult>)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(max_objects) * sizeof(CullResult);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = s_device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&s_results_buffer));
        if (FAILED(hr)) { shutdown(); return false; }
    }

    s_initialized = true;
    std::printf("[DedSec Cull] Initialized: max %u objects\n", max_objects);
    return true;
}

void OcclusionCullSystem::shutdown() {
    if (s_hiz_texture) { s_hiz_texture->Release(); s_hiz_texture = nullptr; }
    if (s_bounds_buffer) { s_bounds_buffer->Release(); s_bounds_buffer = nullptr; }
    if (s_results_buffer) { s_results_buffer->Release(); s_results_buffer = nullptr; }
    if (s_upload_buffer) { s_upload_buffer->Release(); s_upload_buffer = nullptr; }
    if (s_hiz_pso) { s_hiz_pso->Release(); s_hiz_pso = nullptr; }
    if (s_cull_pso) { s_cull_pso->Release(); s_cull_pso = nullptr; }
    if (s_root_sig) { s_root_sig->Release(); s_root_sig = nullptr; }
    if (s_device) { s_device->Release(); s_device = nullptr; }
    s_initialized = false;
    std::printf("[DedSec Cull] Shutdown\n");
}

uint64_t OcclusionCullSystem::build_hiz(
    ID3D12CommandQueue* compute_queue,
    ID3D12Resource* depth_buffer,
    uint32_t width,
    uint32_t height)
{
    if (!s_initialized || !compute_queue || !depth_buffer) return 0;

    // Create or recreate Hi-Z texture if dimensions changed
    // In production, cache this and only recreate on resize
    if (!s_hiz_texture) {
        uint32_t mip_count = count_mips(width, height);
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = next_pow2(width);
        desc.Height = next_pow2(height);
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<uint16_t>(mip_count);
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = s_device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&s_hiz_texture));
        if (FAILED(hr)) {
            std::fprintf(stderr, "[DedSec Cull] Failed to create Hi-Z texture\n");
            return 0;
        }
    }

    // TODO: Dispatch Hi-Z build compute shader
    // For each mip level, dispatch a compute shader that reads the previous mip
    // and writes min(depth) of 2x2 blocks to the current mip.
    // This creates a conservative min-depth pyramid for occlusion testing.
    //
    // The actual shader is in dedsec_hiz_build.hlsl (to be created).
    // For now, return 0 to indicate no fence was signaled.

    return 0;
}

void OcclusionCullSystem::upload_bounds(const CullBounds* bounds, uint32_t count) {
    if (!s_initialized || !bounds || count == 0) return;
    count = std::min(count, s_max_objects);

    // Map upload buffer and copy bounds data
    void* mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    if (SUCCEEDED(s_upload_buffer->Map(0, &read_range, &mapped))) {
        std::memcpy(mapped, bounds, count * sizeof(CullBounds));
        s_upload_buffer->Unmap(0, nullptr);
    }

    // TODO: Record a CopyBufferRegion command to transfer from upload to default heap
    // This should be done via a command list executed on the compute or copy queue.
}

uint64_t OcclusionCullSystem::dispatch_cull(
    ID3D12CommandQueue* compute_queue,
    const float* view_projection_matrix,
    uint32_t object_count)
{
    if (!s_initialized || !compute_queue || !view_projection_matrix) return 0;
    object_count = std::min(object_count, s_max_objects);

    // TODO: Dispatch occlusion culling compute shader
    // Each thread tests one AABB against the Hi-Z:
    // 1. Transform AABB corners to clip space using view_projection_matrix
    // 2. Compute screen-space bounding rect
    // 3. Select appropriate Hi-Z mip based on rect size
    // 4. Sample min-depth from Hi-Z at that mip
    // 5. Compare AABB near-plane depth vs sampled min-depth
    // 6. Write visible/culled result to s_results_buffer
    //
    // Thread group size: 64 threads (one per object)
    // Dispatch: ceil(object_count / 64) groups

    s_last_visible = object_count; // Placeholder: assume all visible until shader runs
    s_last_culled = 0;

    return 0;
}

ID3D12Resource* OcclusionCullSystem::get_results_buffer() {
    return s_results_buffer;
}

uint32_t OcclusionCullSystem::get_visible_count() { return s_last_visible; }
uint32_t OcclusionCullSystem::get_culled_count() { return s_last_culled; }

} // namespace dedsec