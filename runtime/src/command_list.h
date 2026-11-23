#pragma once

// The command-list IR (docs/d3d12_vulkan.md section 3.2): what a D3D12 command list records, kept until
// ExecuteCommandLists replays it on Vulkan (vulkan_replay.h). This slice covers legacy and enhanced barriers and
// render-target and depth-stencil clears (whole views or rectangles), copies and resolves. It has no Windows or
// Vulkan dependency.

#include <array>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace dedsec {

using ResourceId = std::uintptr_t;
using ClearColor = std::array<float, 4>;

// D3D12 enum values from the public d3d12.h, so the IR builds without the Windows SDK.
// d3d12_state_map_test checks every one against d3d12.h on Windows.
namespace d3d12_state {
inline constexpr std::uint32_t common = 0;
inline constexpr std::uint32_t vertex_and_constant_buffer = 0x1;
inline constexpr std::uint32_t index_buffer = 0x2;
inline constexpr std::uint32_t render_target = 0x4;
inline constexpr std::uint32_t unordered_access = 0x8;
inline constexpr std::uint32_t depth_write = 0x10;
inline constexpr std::uint32_t depth_read = 0x20;
inline constexpr std::uint32_t non_pixel_shader_resource = 0x40;
inline constexpr std::uint32_t pixel_shader_resource = 0x80;
inline constexpr std::uint32_t stream_out = 0x100;
inline constexpr std::uint32_t indirect_argument = 0x200;
inline constexpr std::uint32_t copy_dest = 0x400;
inline constexpr std::uint32_t copy_source = 0x800;
inline constexpr std::uint32_t resolve_dest = 0x1000;
inline constexpr std::uint32_t resolve_source = 0x2000;
inline constexpr std::uint32_t raytracing_acceleration_structure = 0x400000;
inline constexpr std::uint32_t shading_rate_source = 0x1000000;
inline constexpr std::uint32_t generic_read = 0xac3;
inline constexpr std::uint32_t present = 0;
} // namespace d3d12_state

// D3D12_BARRIER_SYNC (enhanced barriers).
namespace d3d12_sync {
inline constexpr std::uint32_t none = 0;
inline constexpr std::uint32_t all = 0x1;
inline constexpr std::uint32_t draw = 0x2;
inline constexpr std::uint32_t index_input = 0x4;
inline constexpr std::uint32_t vertex_shading = 0x8;
inline constexpr std::uint32_t pixel_shading = 0x10;
inline constexpr std::uint32_t depth_stencil = 0x20;
inline constexpr std::uint32_t render_target = 0x40;
inline constexpr std::uint32_t compute_shading = 0x80;
inline constexpr std::uint32_t raytracing = 0x100;
inline constexpr std::uint32_t copy = 0x200;
inline constexpr std::uint32_t resolve = 0x400;
inline constexpr std::uint32_t execute_indirect = 0x800;
inline constexpr std::uint32_t predication = 0x800;
inline constexpr std::uint32_t all_shading = 0x1000;
inline constexpr std::uint32_t non_pixel_shading = 0x2000;
inline constexpr std::uint32_t emit_raytracing_acceleration_structure_postbuild_info = 0x4000;
inline constexpr std::uint32_t clear_unordered_access_view = 0x8000;
inline constexpr std::uint32_t video_decode = 0x100000;
inline constexpr std::uint32_t video_process = 0x200000;
inline constexpr std::uint32_t video_encode = 0x400000;
inline constexpr std::uint32_t build_raytracing_acceleration_structure = 0x800000;
inline constexpr std::uint32_t copy_raytracing_acceleration_structure = 0x1000000;
inline constexpr std::uint32_t split = 0x80000000;
} // namespace d3d12_sync

// D3D12_BARRIER_ACCESS.
namespace d3d12_access {
inline constexpr std::uint32_t common = 0;
inline constexpr std::uint32_t vertex_buffer = 0x1;
inline constexpr std::uint32_t constant_buffer = 0x2;
inline constexpr std::uint32_t index_buffer = 0x4;
inline constexpr std::uint32_t render_target = 0x8;
inline constexpr std::uint32_t unordered_access = 0x10;
inline constexpr std::uint32_t depth_stencil_write = 0x20;
inline constexpr std::uint32_t depth_stencil_read = 0x40;
inline constexpr std::uint32_t shader_resource = 0x80;
inline constexpr std::uint32_t stream_output = 0x100;
inline constexpr std::uint32_t indirect_argument = 0x200;
inline constexpr std::uint32_t predication = 0x200;
inline constexpr std::uint32_t copy_dest = 0x400;
inline constexpr std::uint32_t copy_source = 0x800;
inline constexpr std::uint32_t resolve_dest = 0x1000;
inline constexpr std::uint32_t resolve_source = 0x2000;
inline constexpr std::uint32_t raytracing_acceleration_structure_read = 0x4000;
inline constexpr std::uint32_t raytracing_acceleration_structure_write = 0x8000;
inline constexpr std::uint32_t shading_rate_source = 0x10000;
inline constexpr std::uint32_t video_decode_read = 0x20000;
inline constexpr std::uint32_t video_decode_write = 0x40000;
inline constexpr std::uint32_t video_process_read = 0x80000;
inline constexpr std::uint32_t video_process_write = 0x100000;
inline constexpr std::uint32_t video_encode_read = 0x200000;
inline constexpr std::uint32_t video_encode_write = 0x400000;
inline constexpr std::uint32_t no_access = 0x80000000;
} // namespace d3d12_access

// D3D12_BARRIER_LAYOUT.
namespace d3d12_layout {
inline constexpr std::uint32_t undefined = 0xffffffff;
inline constexpr std::uint32_t common = 0;
inline constexpr std::uint32_t present = 0;
inline constexpr std::uint32_t generic_read = 1;
inline constexpr std::uint32_t render_target = 2;
inline constexpr std::uint32_t unordered_access = 3;
inline constexpr std::uint32_t depth_stencil_write = 4;
inline constexpr std::uint32_t depth_stencil_read = 5;
inline constexpr std::uint32_t shader_resource = 6;
inline constexpr std::uint32_t copy_source = 7;
inline constexpr std::uint32_t copy_dest = 8;
inline constexpr std::uint32_t resolve_source = 9;
inline constexpr std::uint32_t resolve_dest = 10;
inline constexpr std::uint32_t shading_rate_source = 11;
inline constexpr std::uint32_t video_decode_read = 12;
inline constexpr std::uint32_t video_decode_write = 13;
inline constexpr std::uint32_t video_process_read = 14;
inline constexpr std::uint32_t video_process_write = 15;
inline constexpr std::uint32_t video_encode_read = 16;
inline constexpr std::uint32_t video_encode_write = 17;
inline constexpr std::uint32_t direct_queue_common = 18;
inline constexpr std::uint32_t direct_queue_generic_read = 19;
inline constexpr std::uint32_t direct_queue_unordered_access = 20;
inline constexpr std::uint32_t direct_queue_shader_resource = 21;
inline constexpr std::uint32_t direct_queue_copy_source = 22;
inline constexpr std::uint32_t direct_queue_copy_dest = 23;
inline constexpr std::uint32_t compute_queue_common = 24;
inline constexpr std::uint32_t compute_queue_generic_read = 25;
inline constexpr std::uint32_t compute_queue_unordered_access = 26;
inline constexpr std::uint32_t compute_queue_shader_resource = 27;
inline constexpr std::uint32_t compute_queue_copy_source = 28;
inline constexpr std::uint32_t compute_queue_copy_dest = 29;
inline constexpr std::uint32_t video_queue_common = 30;
} // namespace d3d12_layout

// D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_BARRIER_FLAGS, D3D12_CLEAR_FLAGS and
// D3D12_TEXTURE_BARRIER_FLAGS.
inline constexpr std::uint32_t all_subresources = 0xffffffff;
inline constexpr std::uint32_t barrier_begin_only = 0x1;
inline constexpr std::uint32_t barrier_end_only = 0x2;
inline constexpr std::uint32_t clear_depth = 0x1;
inline constexpr std::uint32_t clear_stencil = 0x2;
inline constexpr std::uint32_t texture_barrier_discard = 0x1;

// A view captured by value while recording: the CPU descriptor slot may be rewritten after the call.
struct TargetView {
    ResourceId resource;
    std::uint32_t mip;
    std::uint32_t first_layer;
    std::uint32_t layer_count;
};

// D3D12_RECT: right and bottom are exclusive.
struct Rect {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

// D3D12 subresource index: mip + layer * mips + plane * mips * layers, or all_subresources.
struct TransitionBarrier {
    ResourceId resource;
    std::uint32_t subresource;
    std::uint32_t before;
    std::uint32_t after;
};

// resource 0 means every UAV access, as a null pResource does in D3D12.
struct UavBarrier {
    ResourceId resource;
};

// Either resource may be 0 ("any placed or reserved resource").
struct AliasingBarrier {
    ResourceId before;
    ResourceId after;
};

// Enhanced barriers (ID3D12GraphicsCommandList7::Barrier).
struct GlobalBarrier {
    std::uint32_t sync_before;
    std::uint32_t sync_after;
    std::uint32_t access_before;
    std::uint32_t access_after;
};

// Offset and size are not kept: D3D12 requires 0 and UINT64_MAX (the whole buffer).
struct BufferBarrier {
    std::uint32_t sync_before;
    std::uint32_t sync_after;
    std::uint32_t access_before;
    std::uint32_t access_after;
    ResourceId resource;
};

// D3D12_BARRIER_SUBRESOURCE_RANGE: mip_count == 0 means index_or_first_mip is a subresource index
// (all_subresources for every one).
struct BarrierSubresourceRange {
    std::uint32_t index_or_first_mip;
    std::uint32_t mip_count;
    std::uint32_t first_layer;
    std::uint32_t layer_count;
    std::uint32_t first_plane;
    std::uint32_t plane_count;
};

struct TextureBarrier {
    std::uint32_t sync_before;
    std::uint32_t sync_after;
    std::uint32_t access_before;
    std::uint32_t access_after;
    std::uint32_t layout_before;
    std::uint32_t layout_after;
    ResourceId resource;
    BarrierSubresourceRange range;
    std::uint32_t flags;  // texture_barrier_discard
};

// rects empty: the whole view.
// CopyBufferRegion.
struct CopyBuffer {
    ResourceId dst;
    std::uint64_t dst_offset;
    ResourceId src;
    std::uint64_t src_offset;
    std::uint64_t size;
};

// D3D12_TEXTURE_COPY_LOCATION: a texture subresource, or a placed footprint in a buffer. The footprint's format is
// not kept: the copy uses the texture's own format for the plane, which D3D12 requires to be compatible.
struct CopyLocation {
    ResourceId resource;
    bool footprint;
    std::uint32_t subresource;  // texture
    std::uint64_t offset;       // footprint: byte offset in the buffer
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t depth;
    std::uint32_t row_pitch;
};

// D3D12_BOX: right, bottom and back are exclusive.
struct CopyBox {
    std::uint32_t left;
    std::uint32_t top;
    std::uint32_t front;
    std::uint32_t right;
    std::uint32_t bottom;
    std::uint32_t back;
};

// CopyTextureRegion. Without a box, the whole source subresource or footprint is copied.
struct CopyTexture {
    CopyLocation dst;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;
    CopyLocation src;
    bool has_box;
    CopyBox box;
};

// CopyResource: every subresource, or the whole buffer.
struct CopyResource {
    ResourceId dst;
    ResourceId src;
};

// ResolveSubresource: a multisampled subresource into a single-sampled one of the same size. The call's format is
// not kept: the resolve uses the textures' own format, which D3D12 requires to be compatible.
struct ResolveSubresource {
    ResourceId dst;
    std::uint32_t dst_subresource;
    ResourceId src;
    std::uint32_t src_subresource;
};

// D3D12_TEXTURE_DATA_PITCH_ALIGNMENT and D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT.
inline constexpr std::uint32_t texture_data_pitch_alignment = 256;
inline constexpr std::uint32_t texture_data_placement_alignment = 512;

struct ClearRenderTarget {
    TargetView target;
    ClearColor color;
    std::vector<Rect> rects;
};

struct ClearDepthStencil {
    TargetView target;
    std::uint32_t flags;  // clear_depth | clear_stencil
    float depth;
    std::uint8_t stencil;
    std::vector<Rect> rects;
};

// Pipeline state object reference: opaque handle captured at SetPipelineState time.
// The replay backend resolves this to a Vulkan pipeline or DX12 PSO.
struct SetPipelineState {
    ResourceId pso;
};

// Root signature + root constants/tables. Captured at SetGraphicsRootSignature time.
struct SetRootSignature {
    ResourceId root_signature;
};

// D3D12_GPU_VIRTUAL_ADDRESS for buffer views (VB, IB, CBV, SRV, UAV).
using GpuVirtualAddress = std::uint64_t;

// IASetVertexBuffers
struct SetVertexBuffer {
    std::uint32_t slot;
    GpuVirtualAddress address;
    std::uint32_t stride;
    std::uint32_t size;
};

// IASetIndexBuffer
enum class IndexFormat : std::uint32_t {
    uint16 = 0,  // DXGI_FORMAT_R16_UINT
    uint32 = 1,  // DXGI_FORMAT_R32_UINT
};

struct SetIndexBuffer {
    GpuVirtualAddress address;
    IndexFormat format;
    std::uint32_t size;
};

// RSSetViewports
struct Viewport {
    float top_left_x;
    float top_left_y;
    float width;
    float height;
    float min_depth;
    float max_depth;
};

struct SetViewports {
    std::vector<Viewport> viewports;
};

// RSSetScissorRects
struct SetScissors {
    std::vector<Rect> scissors;
};

// OMSetRenderTargets
struct SetRenderTargets {
    std::vector<TargetView> render_targets;  // up to 8
    bool single_handle_to_range;             // if true, first entry is range start
    TargetView depth_stencil;                // resource==0 means no DSV
};

// DrawInstanced
struct DrawInstanced {
    std::uint32_t vertex_count;
    std::uint32_t instance_count;
    std::uint32_t start_vertex;
    std::uint32_t start_instance;
};

// DrawIndexedInstanced
struct DrawIndexedInstanced {
    std::uint32_t index_count;
    std::uint32_t instance_count;
    std::uint32_t start_index;
    std::int32_t base_vertex;
    std::uint32_t start_instance;
};

// Dispatch
struct Dispatch {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;
};

// SetGraphicsRootConstant / RootConstants
struct SetRootConstants {
    std::uint32_t root_parameter_index;
    std::uint32_t num_values;
    std::vector<std::uint32_t> values;
};

// SetGraphicsRootDescriptorTable / SetComputeRootDescriptorTable (bindless GPU VA)
struct SetRootDescriptorTable {
    std::uint32_t root_parameter_index;
    GpuVirtualAddress gpu_address;
    bool compute;  // true = compute root, false = graphics root
};

// SetDescriptorHeaps — only CBV_SRV_UAV and RTV heaps are allowed, max 2
struct SetDescriptorHeaps {
    ResourceId heaps[2];       // resource IDs of descriptor heaps
    std::uint32_t heap_count;  // 0, 1 or 2
};

// SetComputeRootSignature — separate from graphics root signature
struct SetComputeRootSignature {
    ResourceId root_signature;
};

// ClearUnorderedAccessViewUint / ClearUnorderedAccessViewFloat
struct ClearUav {
    TargetView target;
    std::array<std::uint32_t, 4> uint_values;
    ClearColor float_values;
    bool use_float;            // true = float clear, false = uint clear
    std::vector<Rect> rects;
};

// ExecuteIndirect — indirect draw/dispatch via buffer
enum class IndirectCommandType : std::uint32_t {
    draw = 0,
    draw_indexed = 1,
    dispatch = 2,
};

struct ExecuteIndirect {
    IndirectCommandType type;
    ResourceId command_buffer;
    std::uint64_t command_buffer_offset;
    std::uint32_t max_command_count;
    ResourceId count_buffer;           // 0 = no count buffer
    std::uint64_t count_buffer_offset;
};

// SetPredication — conditional rendering
struct SetPredication {
    ResourceId buffer;
    std::uint64_t offset;
    bool condition_equal_zero;  // true = skip if zero, false = skip if non-zero
};

// SOSetTargets — stream output targets
struct SoTarget {
    ResourceId resource;
    std::uint64_t offset;
    std::uint64_t size;
};

struct SetStreamOutputTargets {
    std::vector<SoTarget> targets;  // up to 4
    bool append;
};

// SetBlendFactor
struct SetBlendFactor {
    float factor[4];
};

// SetStencilRef
struct SetStencilRef {
    std::uint32_t stencil_ref;
};

// SetSamplePositions (MSAA sample positions)
struct SamplePosition {
    std::int8_t x;
    std::int8_t y;
};

struct SetSamplePositions {
    std::uint32_t sample_count;
    std::uint32_t pixel_count;
    std::vector<SamplePosition> positions;
};

// SetShadingRate (VRS)
enum class ShadingRate : std::uint32_t {
    rate_1x1 = 0,
    rate_1x2 = 1,
    rate_2x1 = 2,
    rate_2x2 = 3,
    rate_2x4 = 4,
    rate_4x2 = 5,
    rate_4x4 = 6,
};

struct SetShadingRate {
    ShadingRate base_rate;
    std::uint32_t combiner_ops[2];  // D3D12_SHADING_RATE_COMBINER_OP
};

// RSSetShadingRateImage
struct SetShadingRateImage {
    ResourceId resource;
};

using Command = std::variant<TransitionBarrier, UavBarrier, AliasingBarrier, GlobalBarrier, BufferBarrier,
                             TextureBarrier, ClearRenderTarget, ClearDepthStencil, ClearUav,
                             CopyBuffer, CopyTexture, CopyResource, ResolveSubresource,
                             SetPipelineState, SetRootSignature, SetComputeRootSignature,
                             SetDescriptorHeaps, SetVertexBuffer, SetIndexBuffer,
                             SetViewports, SetScissors, SetRenderTargets,
                             DrawInstanced, DrawIndexedInstanced, Dispatch,
                             SetRootConstants, SetRootDescriptorTable,
                             ExecuteIndirect, SetPredication, SetStreamOutputTargets,
                             SetBlendFactor, SetStencilRef, SetSamplePositions,
                             SetShadingRate, SetShadingRateImage>;

class CommandList {
public:
    // D3D12 split barriers: the begin half only marks the resource unusable until the end half, so the transition
    // is recorded once, at the end. A full barrier (flags 0) is recorded where it is issued.
    bool transition(const TransitionBarrier& barrier, std::uint32_t flags = 0) {
        if (closed_ || !barrier.resource) return false;
        if (flags & ~(barrier_begin_only | barrier_end_only)) return false;
        if (flags == (barrier_begin_only | barrier_end_only)) return false;
        if (flags & barrier_begin_only) return true;
        commands_.push_back(barrier);
        return true;
    }

    bool uav_barrier(const UavBarrier& barrier) { return append(barrier); }
    bool aliasing_barrier(const AliasingBarrier& barrier) { return append(barrier); }

    // Enhanced split barriers follow the same rule: the half with sync_after == split is dropped, and the half with
    // sync_before == split is replayed as a full barrier.
    bool barrier(const GlobalBarrier& barrier) {
        if (!valid_sync(barrier.sync_before, barrier.sync_after)) return false;
        if (barrier.sync_after == d3d12_sync::split) return !closed_;
        return append(barrier);
    }

    bool barrier(const BufferBarrier& barrier) {
        if (!barrier.resource || !valid_sync(barrier.sync_before, barrier.sync_after)) return false;
        if (barrier.sync_after == d3d12_sync::split) return !closed_;
        return append(barrier);
    }

    bool barrier(const TextureBarrier& barrier) {
        if (!barrier.resource || !valid_sync(barrier.sync_before, barrier.sync_after)) return false;
        if (barrier.layout_after == d3d12_layout::undefined && barrier.layout_before != d3d12_layout::undefined)
            return false;
        if (barrier.flags & ~texture_barrier_discard) return false;
        if (barrier.range.mip_count && (!barrier.range.layer_count || !barrier.range.plane_count)) return false;
        if (barrier.sync_after == d3d12_sync::split) return !closed_;
        return append(barrier);
    }

    // Copies. The rules D3D12 states for the call itself are checked here; ranges against the resources are
    // checked at replay, when the resources are known.
    bool copy_buffer(const CopyBuffer& copy) {
        if (!copy.dst || !copy.src || !copy.size) return false;
        return append(copy);
    }

    bool copy_texture(const CopyTexture& copy) {
        if (!valid(copy.dst) || !valid(copy.src) || (copy.dst.footprint && copy.src.footprint)) return false;
        if (copy.has_box && (copy.box.right <= copy.box.left || copy.box.bottom <= copy.box.top ||
                             copy.box.back <= copy.box.front))
            return false;
        return append(copy);
    }

    bool copy_resource(const CopyResource& copy) {
        if (!copy.dst || !copy.src || copy.dst == copy.src) return false;
        return append(copy);
    }

    // A resource has one sample count, so it cannot be resolved into itself.
    bool resolve_subresource(const ResolveSubresource& resolve) {
        if (!resolve.dst || !resolve.src || resolve.dst == resolve.src) return false;
        return append(resolve);
    }

    // ClearRenderTargetView; the resource must be in render_target state at execution. No rects: the whole view.
    bool clear_render_target(const TargetView& target, const ClearColor& color, std::span<const Rect> rects = {}) {
        if (!valid(target)) return false;
        return append(ClearRenderTarget{target, color, {rects.begin(), rects.end()}});
    }

    // ClearDepthStencilView; the cleared planes must be in depth_write state at execution.
    // D3D12 requires depth in [0, 1] and at least one clear flag.
    bool clear_depth_stencil(const TargetView& target, std::uint32_t flags, float depth, std::uint8_t stencil,
                             std::span<const Rect> rects = {}) {
        if (!valid(target)) return false;
        if (!flags || (flags & ~(clear_depth | clear_stencil))) return false;
        if ((flags & clear_depth) && !(depth >= 0.0f && depth <= 1.0f)) return false;
        return append(ClearDepthStencil{target, flags, depth, stencil, {rects.begin(), rects.end()}});
    }

    // --- Pipeline State & Root Signature ---

    bool set_pipeline_state(ResourceId pso) {
        if (!pso) return false;
        return append(SetPipelineState{pso});
    }

    bool set_root_signature(ResourceId root_signature) {
        if (!root_signature) return false;
        return append(SetRootSignature{root_signature});
    }

    // --- Input Assembly ---

    bool set_vertex_buffer(std::uint32_t slot, GpuVirtualAddress address, std::uint32_t stride, std::uint32_t size) {
        if (!address || !stride || !size) return false;
        return append(SetVertexBuffer{slot, address, stride, size});
    }

    bool set_index_buffer(GpuVirtualAddress address, IndexFormat format, std::uint32_t size) {
        if (!address || !size) return false;
        return append(SetIndexBuffer{address, format, size});
    }

    // --- Rasterizer State ---

    bool set_viewports(std::span<const Viewport> viewports) {
        if (viewports.empty() || viewports.size() > 16) return false;
        for (const auto& vp : viewports) {
            if (vp.width <= 0.0f || vp.height <= 0.0f) return false;
            if (!(vp.min_depth >= 0.0f && vp.max_depth <= 1.0f && vp.min_depth <= vp.max_depth)) return false;
        }
        return append(SetViewports{{viewports.begin(), viewports.end()}});
    }

    bool set_scissors(std::span<const Rect> scissors) {
        if (scissors.empty() || scissors.size() > 16) return false;
        for (const auto& r : scissors) {
            if (r.right <= r.left || r.bottom <= r.top) return false;
        }
        return append(SetScissors{{scissors.begin(), scissors.end()}});
    }

    // --- Output Merger ---

    bool set_render_targets(std::span<const TargetView> render_targets, bool single_handle_to_range,
                            const TargetView& depth_stencil = {}) {
        if (render_targets.size() > 8) return false;
        for (const auto& rt : render_targets) {
            if (!valid(rt)) return false;
        }
        if (depth_stencil.resource && !valid(depth_stencil)) return false;
        return append(SetRenderTargets{{render_targets.begin(), render_targets.end()},
                                       single_handle_to_range, depth_stencil});
    }

    // --- Draw Commands ---

    bool draw_instanced(std::uint32_t vertex_count, std::uint32_t instance_count,
                        std::uint32_t start_vertex = 0, std::uint32_t start_instance = 0) {
        if (!vertex_count || !instance_count) return false;
        return append(DrawInstanced{vertex_count, instance_count, start_vertex, start_instance});
    }

    bool draw_indexed_instanced(std::uint32_t index_count, std::uint32_t instance_count,
                                std::uint32_t start_index = 0, std::int32_t base_vertex = 0,
                                std::uint32_t start_instance = 0) {
        if (!index_count || !instance_count) return false;
        return append(DrawIndexedInstanced{index_count, instance_count, start_index, base_vertex, start_instance});
    }

    // --- Compute Dispatch ---

    bool dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        if (!x || !y || !z) return false;
        return append(Dispatch{x, y, z});
    }

    // --- Root Parameters ---

    bool set_root_constants(std::uint32_t root_parameter_index, std::span<const std::uint32_t> values) {
        if (values.empty()) return false;
        return append(SetRootConstants{root_parameter_index, static_cast<std::uint32_t>(values.size()),
                                       {values.begin(), values.end()}});
    }

    bool set_root_descriptor_table(std::uint32_t root_parameter_index, GpuVirtualAddress gpu_address,
                                   bool compute = false) {
        if (!gpu_address) return false;
        return append(SetRootDescriptorTable{root_parameter_index, gpu_address, compute});
    }

    // --- Compute Root Signature (separate from graphics) ---

    bool set_compute_root_signature(ResourceId root_signature) {
        if (!root_signature) return false;
        return append(SetComputeRootSignature{root_signature});
    }

    // --- Descriptor Heaps ---

    bool set_descriptor_heaps(std::span<const ResourceId> heaps) {
        if (heaps.size() > 2) return false;
        SetDescriptorHeaps cmd{};
        cmd.heap_count = static_cast<std::uint32_t>(heaps.size());
        for (std::uint32_t i = 0; i < cmd.heap_count; ++i) cmd.heaps[i] = heaps[i];
        return append(cmd);
    }

    // --- Clear UAV ---

    bool clear_uav_float(const TargetView& target, const ClearColor& values, std::span<const Rect> rects = {}) {
        if (!valid(target)) return false;
        return append(ClearUav{target, {}, values, true, {rects.begin(), rects.end()}});
    }

    bool clear_uav_uint(const TargetView& target, const std::array<std::uint32_t, 4>& values,
                        std::span<const Rect> rects = {}) {
        if (!valid(target)) return false;
        return append(ClearUav{target, values, {}, false, {rects.begin(), rects.end()}});
    }

    // --- Execute Indirect ---

    bool execute_indirect(IndirectCommandType type, ResourceId command_buffer, std::uint64_t offset,
                          std::uint32_t max_count, ResourceId count_buffer = 0, std::uint64_t count_offset = 0) {
        if (!command_buffer || !max_count) return false;
        return append(ExecuteIndirect{type, command_buffer, offset, max_count, count_buffer, count_offset});
    }

    // --- Predication ---

    bool set_predication(ResourceId buffer, std::uint64_t offset, bool condition_equal_zero) {
        return append(SetPredication{buffer, offset, condition_equal_zero});
    }

    // --- Stream Output ---

    bool set_stream_output_targets(std::span<const SoTarget> targets, bool append_mode = false) {
        if (targets.size() > 4) return false;
        return append(SetStreamOutputTargets{{targets.begin(), targets.end()}, append_mode});
    }

    // --- Blend Factor & Stencil Ref ---

    bool set_blend_factor(float r, float g, float b, float a) {
        return append(SetBlendFactor{{r, g, b, a}});
    }

    bool set_stencil_ref(std::uint32_t ref) {
        return append(SetStencilRef{ref});
    }

    // --- Sample Positions (MSAA) ---

    bool set_sample_positions(std::uint32_t sample_count, std::uint32_t pixel_count,
                              std::span<const SamplePosition> positions) {
        if (!sample_count || !pixel_count || positions.empty()) return false;
        return append(SetSamplePositions{sample_count, pixel_count, {positions.begin(), positions.end()}});
    }

    // --- Variable Rate Shading ---

    bool set_shading_rate(ShadingRate rate, std::uint32_t combiner0 = 0, std::uint32_t combiner1 = 0) {
        return append(SetShadingRate{rate, {combiner0, combiner1}});
    }

    bool set_shading_rate_image(ResourceId resource) {
        return append(SetShadingRateImage{resource});
    }

    // --- Serialization ---

    // Serialize all commands into a flat byte buffer for replay or disk cache.
    // Format: [uint32_t command_count][tag + payload]...
    // Returns empty vector on failure.
    std::vector<std::uint8_t> serialize() const;

    // Deserialize from a buffer produced by serialize(). Returns false on any corruption.
    bool deserialize(std::span<const std::uint8_t> data);

    // --- Ring Buffer Allocator (zero-alloc per frame) ---

    // Pre-allocated ring buffer for command storage. Avoids heap allocation during recording.
    // Capacity is in number of Command slots.
    class RingAllocator {
    public:
        explicit RingAllocator(std::size_t capacity = 4096);
        ~RingAllocator();

        // Non-copyable, movable
        RingAllocator(const RingAllocator&) = delete;
        RingAllocator& operator=(const RingAllocator&) = delete;
        RingAllocator(RingAllocator&& other) noexcept;
        RingAllocator& operator=(RingAllocator&& other) noexcept;

        // Allocate a slot in the ring. Returns nullptr if full.
        Command* allocate();

        // Reset the write cursor (call once per frame after submission).
        void reset();

        // Number of commands currently stored.
        std::size_t size() const { return write_pos_ - read_pos_; }

        // Whether the ring is full.
        bool full() const { return size() >= capacity_; }

        // Iterate stored commands.
        std::span<const Command> commands() const;

        std::size_t capacity() const { return capacity_; }

    private:
        Command* buffer_;
        std::size_t capacity_;
        std::size_t write_pos_;
        std::size_t read_pos_;
    };

    bool close() {
        if (closed_) return false;
        closed_ = true;
        return true;
    }

    bool reset() {
        if (!closed_) return false;
        commands_.clear();
        closed_ = false;
        return true;
    }

    bool closed() const { return closed_; }
    std::span<const Command> commands() const { return commands_; }

private:
    static bool valid(const TargetView& target) { return target.resource && target.layer_count; }

    // A placed footprint needs aligned offsets and pitches and a non-empty size.
    static bool valid(const CopyLocation& location) {
        if (!location.resource) return false;
        if (!location.footprint) return true;
        return location.width && location.height && location.depth && location.row_pitch &&
               location.offset % texture_data_placement_alignment == 0 &&
               location.row_pitch % texture_data_pitch_alignment == 0;
    }

    // A split begin and end cannot be the same barrier.
    static bool valid_sync(std::uint32_t before, std::uint32_t after) {
        return !(before == d3d12_sync::split && after == d3d12_sync::split);
    }

    bool append(const Command& command) {
        if (closed_) return false;
        commands_.push_back(command);
        return true;
    }

    std::vector<Command> commands_;
    bool closed_ = false;
};

} // namespace dedsec
