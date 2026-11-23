// DedSec Command List IR — Ring Buffer + Serialization
// Zero-allocation per-frame command recording and flat-buffer serialization for replay.
// Original DedSec implementation; not copied from XCL or Leonida.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "command_list.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace dedsec {

// ---------------------------------------------------------------------------
// Tag IDs for serialization. Each Command variant gets a unique tag.
// ---------------------------------------------------------------------------
enum class CmdTag : std::uint32_t {
    transition_barrier = 1,
    uav_barrier,
    aliasing_barrier,
    global_barrier,
    buffer_barrier,
    texture_barrier,
    clear_render_target,
    clear_depth_stencil,
    clear_uav,
    copy_buffer,
    copy_texture,
    copy_resource,
    resolve_subresource,
    set_pipeline_state,
    set_root_signature,
    set_compute_root_signature,
    set_descriptor_heaps,
    set_vertex_buffer,
    set_index_buffer,
    set_viewports,
    set_scissors,
    set_render_targets,
    draw_instanced,
    draw_indexed_instanced,
    dispatch_cmd,
    set_root_constants,
    set_root_descriptor_table,
    execute_indirect,
    set_predication,
    set_stream_output_targets,
    set_blend_factor,
    set_stencil_ref,
    set_sample_positions,
    set_shading_rate,
    set_shading_rate_image,
};

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------
namespace {

template <typename T>
void write_pod(std::vector<std::uint8_t>& buf, const T& value) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
    buf.insert(buf.end(), p, p + sizeof(T));
}

template <typename T>
bool read_pod(const std::uint8_t*& ptr, const std::uint8_t* end, T& out) {
    if (ptr + sizeof(T) > end) return false;
    std::memcpy(&out, ptr, sizeof(T));
    ptr += sizeof(T);
    return true;
}

void write_tag(std::vector<std::uint8_t>& buf, CmdTag tag) {
    write_pod(buf, static_cast<std::uint32_t>(tag));
}

bool read_tag(const std::uint8_t*& ptr, const std::uint8_t* end, CmdTag& tag) {
    std::uint32_t v;
    if (!read_pod(ptr, end, v)) return false;
    tag = static_cast<CmdTag>(v);
    return true;
}

void write_rects(std::vector<std::uint8_t>& buf, const std::vector<Rect>& rects) {
    write_pod(buf, static_cast<std::uint32_t>(rects.size()));
    for (const auto& r : rects) write_pod(buf, r);
}

bool read_rects(const std::uint8_t*& ptr, const std::uint8_t* end, std::vector<Rect>& out) {
    std::uint32_t count;
    if (!read_pod(ptr, end, count)) return false;
    if (count > 4096) return false;
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!read_pod(ptr, end, out[i])) return false;
    }
    return true;
}

void write_viewports(std::vector<std::uint8_t>& buf, const std::vector<Viewport>& vps) {
    write_pod(buf, static_cast<std::uint32_t>(vps.size()));
    for (const auto& vp : vps) write_pod(buf, vp);
}

bool read_viewports(const std::uint8_t*& ptr, const std::uint8_t* end, std::vector<Viewport>& out) {
    std::uint32_t count;
    if (!read_pod(ptr, end, count)) return false;
    if (count > 16) return false;
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!read_pod(ptr, end, out[i])) return false;
    }
    return true;
}

void write_target_view(std::vector<std::uint8_t>& buf, const TargetView& tv) {
    write_pod(buf, tv);
}

bool read_target_view(const std::uint8_t*& ptr, const std::uint8_t* end, TargetView& tv) {
    return read_pod(ptr, end, tv);
}

void write_copy_location(std::vector<std::uint8_t>& buf, const CopyLocation& cl) {
    write_pod(buf, cl);
}

bool read_copy_location(const std::uint8_t*& ptr, const std::uint8_t* end, CopyLocation& cl) {
    return read_pod(ptr, end, cl);
}

void write_u32_values(std::vector<std::uint8_t>& buf, const std::vector<std::uint32_t>& vals) {
    write_pod(buf, static_cast<std::uint32_t>(vals.size()));
    for (auto v : vals) write_pod(buf, v);
}

bool read_u32_values(const std::uint8_t*& ptr, const std::uint8_t* end, std::vector<std::uint32_t>& out) {
    std::uint32_t count;
    if (!read_pod(ptr, end, count)) return false;
    if (count > 4096) return false;
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!read_pod(ptr, end, out[i])) return false;
    }
    return true;
}

// Serialize one command into buf. Returns false on unknown variant (should never happen).
bool serialize_command(std::vector<std::uint8_t>& buf, const Command& cmd) {
    return std::visit([&buf](const auto& c) -> bool {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, TransitionBarrier>) {
            write_tag(buf, CmdTag::transition_barrier);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, UavBarrier>) {
            write_tag(buf, CmdTag::uav_barrier);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, AliasingBarrier>) {
            write_tag(buf, CmdTag::aliasing_barrier);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, GlobalBarrier>) {
            write_tag(buf, CmdTag::global_barrier);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, BufferBarrier>) {
            write_tag(buf, CmdTag::buffer_barrier);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, TextureBarrier>) {
            write_tag(buf, CmdTag::texture_barrier);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, ClearRenderTarget>) {
            write_tag(buf, CmdTag::clear_render_target);
            write_target_view(buf, c.target);
            write_pod(buf, c.color);
            write_rects(buf, c.rects);
        } else if constexpr (std::is_same_v<T, ClearDepthStencil>) {
            write_tag(buf, CmdTag::clear_depth_stencil);
            write_target_view(buf, c.target);
            write_pod(buf, c.flags);
            write_pod(buf, c.depth);
            write_pod(buf, c.stencil);
            write_rects(buf, c.rects);
        } else if constexpr (std::is_same_v<T, ClearUav>) {
            write_tag(buf, CmdTag::clear_uav);
            write_target_view(buf, c.target);
            write_pod(buf, c.uint_values);
            write_pod(buf, c.float_values);
            write_pod(buf, c.use_float);
            write_rects(buf, c.rects);
        } else if constexpr (std::is_same_v<T, CopyBuffer>) {
            write_tag(buf, CmdTag::copy_buffer);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, CopyTexture>) {
            write_tag(buf, CmdTag::copy_texture);
            write_copy_location(buf, c.dst);
            write_pod(buf, c.x);
            write_pod(buf, c.y);
            write_pod(buf, c.z);
            write_copy_location(buf, c.src);
            write_pod(buf, c.has_box);
            if (c.has_box) write_pod(buf, c.box);
        } else if constexpr (std::is_same_v<T, CopyResource>) {
            write_tag(buf, CmdTag::copy_resource);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, ResolveSubresource>) {
            write_tag(buf, CmdTag::resolve_subresource);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetPipelineState>) {
            write_tag(buf, CmdTag::set_pipeline_state);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetRootSignature>) {
            write_tag(buf, CmdTag::set_root_signature);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetComputeRootSignature>) {
            write_tag(buf, CmdTag::set_compute_root_signature);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetDescriptorHeaps>) {
            write_tag(buf, CmdTag::set_descriptor_heaps);
            write_pod(buf, c.heap_count);
            for (std::uint32_t i = 0; i < c.heap_count; ++i) write_pod(buf, c.heaps[i]);
        } else if constexpr (std::is_same_v<T, SetVertexBuffer>) {
            write_tag(buf, CmdTag::set_vertex_buffer);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetIndexBuffer>) {
            write_tag(buf, CmdTag::set_index_buffer);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetViewports>) {
            write_tag(buf, CmdTag::set_viewports);
            write_viewports(buf, c.viewports);
        } else if constexpr (std::is_same_v<T, SetScissors>) {
            write_tag(buf, CmdTag::set_scissors);
            write_rects(buf, c.scissors);
        } else if constexpr (std::is_same_v<T, SetRenderTargets>) {
            write_tag(buf, CmdTag::set_render_targets);
            write_pod(buf, static_cast<std::uint32_t>(c.render_targets.size()));
            for (const auto& rt : c.render_targets) write_target_view(buf, rt);
            write_pod(buf, c.single_handle_to_range);
            write_target_view(buf, c.depth_stencil);
        } else if constexpr (std::is_same_v<T, DrawInstanced>) {
            write_tag(buf, CmdTag::draw_instanced);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, DrawIndexedInstanced>) {
            write_tag(buf, CmdTag::draw_indexed_instanced);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, Dispatch>) {
            write_tag(buf, CmdTag::dispatch_cmd);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetRootConstants>) {
            write_tag(buf, CmdTag::set_root_constants);
            write_pod(buf, c.root_parameter_index);
            write_u32_values(buf, c.values);
        } else if constexpr (std::is_same_v<T, SetRootDescriptorTable>) {
            write_tag(buf, CmdTag::set_root_descriptor_table);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, ExecuteIndirect>) {
            write_tag(buf, CmdTag::execute_indirect);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetPredication>) {
            write_tag(buf, CmdTag::set_predication);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetStreamOutputTargets>) {
            write_tag(buf, CmdTag::set_stream_output_targets);
            write_pod(buf, static_cast<std::uint32_t>(c.targets.size()));
            for (const auto& t : c.targets) write_pod(buf, t);
            write_pod(buf, c.append);
        } else if constexpr (std::is_same_v<T, SetBlendFactor>) {
            write_tag(buf, CmdTag::set_blend_factor);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetStencilRef>) {
            write_tag(buf, CmdTag::set_stencil_ref);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetSamplePositions>) {
            write_tag(buf, CmdTag::set_sample_positions);
            write_pod(buf, c.sample_count);
            write_pod(buf, c.pixel_count);
            write_pod(buf, static_cast<std::uint32_t>(c.positions.size()));
            for (const auto& p : c.positions) write_pod(buf, p);
        } else if constexpr (std::is_same_v<T, SetShadingRate>) {
            write_tag(buf, CmdTag::set_shading_rate);
            write_pod(buf, c);
        } else if constexpr (std::is_same_v<T, SetShadingRateImage>) {
            write_tag(buf, CmdTag::set_shading_rate_image);
            write_pod(buf, c);
        } else {
            return false;
        }
        return true;
    }, cmd);
}

// Deserialize one command from ptr. Advances ptr past the consumed bytes.
bool deserialize_command(const std::uint8_t*& ptr, const std::uint8_t* end, Command& out) {
    CmdTag tag;
    if (!read_tag(ptr, end, tag)) return false;

    switch (tag) {
    case CmdTag::transition_barrier: {
        TransitionBarrier c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::uav_barrier: {
        UavBarrier c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::aliasing_barrier: {
        AliasingBarrier c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::global_barrier: {
        GlobalBarrier c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::buffer_barrier: {
        BufferBarrier c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::texture_barrier: {
        TextureBarrier c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::clear_render_target: {
        ClearRenderTarget c;
        if (!read_target_view(ptr, end, c.target)) return false;
        if (!read_pod(ptr, end, c.color)) return false;
        if (!read_rects(ptr, end, c.rects)) return false;
        out = std::move(c);
        return true;
    }
    case CmdTag::clear_depth_stencil: {
        ClearDepthStencil c;
        if (!read_target_view(ptr, end, c.target)) return false;
        if (!read_pod(ptr, end, c.flags)) return false;
        if (!read_pod(ptr, end, c.depth)) return false;
        if (!read_pod(ptr, end, c.stencil)) return false;
        if (!read_rects(ptr, end, c.rects)) return false;
        out = std::move(c);
        return true;
    }
    case CmdTag::clear_uav: {
        ClearUav c;
        if (!read_target_view(ptr, end, c.target)) return false;
        if (!read_pod(ptr, end, c.uint_values)) return false;
        if (!read_pod(ptr, end, c.float_values)) return false;
        if (!read_pod(ptr, end, c.use_float)) return false;
        if (!read_rects(ptr, end, c.rects)) return false;
        out = std::move(c);
        return true;
    }
    case CmdTag::copy_buffer: {
        CopyBuffer c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::copy_texture: {
        CopyTexture c;
        if (!read_copy_location(ptr, end, c.dst)) return false;
        if (!read_pod(ptr, end, c.x)) return false;
        if (!read_pod(ptr, end, c.y)) return false;
        if (!read_pod(ptr, end, c.z)) return false;
        if (!read_copy_location(ptr, end, c.src)) return false;
        if (!read_pod(ptr, end, c.has_box)) return false;
        if (c.has_box && !read_pod(ptr, end, c.box)) return false;
        out = c;
        return true;
    }
    case CmdTag::copy_resource: {
        CopyResource c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::resolve_subresource: {
        ResolveSubresource c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_pipeline_state: {
        SetPipelineState c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_root_signature: {
        SetRootSignature c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_compute_root_signature: {
        SetComputeRootSignature c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_descriptor_heaps: {
        SetDescriptorHeaps c{};
        if (!read_pod(ptr, end, c.heap_count)) return false;
        if (c.heap_count > 2) return false;
        for (std::uint32_t i = 0; i < c.heap_count; ++i) {
            if (!read_pod(ptr, end, c.heaps[i])) return false;
        }
        out = c;
        return true;
    }
    case CmdTag::set_vertex_buffer: {
        SetVertexBuffer c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_index_buffer: {
        SetIndexBuffer c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_viewports: {
        SetViewports c;
        if (!read_viewports(ptr, end, c.viewports)) return false;
        out = std::move(c);
        return true;
    }
    case CmdTag::set_scissors: {
        SetScissors c;
        if (!read_rects(ptr, end, c.scissors)) return false;
        out = std::move(c);
        return true;
    }
    case CmdTag::set_render_targets: {
        SetRenderTargets c;
        std::uint32_t rt_count;
        if (!read_pod(ptr, end, rt_count)) return false;
        if (rt_count > 8) return false;
        c.render_targets.resize(rt_count);
        for (std::uint32_t i = 0; i < rt_count; ++i) {
            if (!read_target_view(ptr, end, c.render_targets[i])) return false;
        }
        if (!read_pod(ptr, end, c.single_handle_to_range)) return false;
        if (!read_target_view(ptr, end, c.depth_stencil)) return false;
        out = std::move(c);
        return true;
    }
    case CmdTag::draw_instanced: {
        DrawInstanced c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::draw_indexed_instanced: {
        DrawIndexedInstanced c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::dispatch_cmd: {
        Dispatch c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_root_constants: {
        SetRootConstants c;
        if (!read_pod(ptr, end, c.root_parameter_index)) return false;
        if (!read_u32_values(ptr, end, c.values)) return false;
        c.num_values = static_cast<std::uint32_t>(c.values.size());
        out = std::move(c);
        return true;
    }
    case CmdTag::set_root_descriptor_table: {
        SetRootDescriptorTable c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::execute_indirect: {
        ExecuteIndirect c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_predication: {
        SetPredication c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_stream_output_targets: {
        SetStreamOutputTargets c;
        std::uint32_t count;
        if (!read_pod(ptr, end, count)) return false;
        if (count > 4) return false;
        c.targets.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!read_pod(ptr, end, c.targets[i])) return false;
        }
        if (!read_pod(ptr, end, c.append)) return false;
        out = std::move(c);
        return true;
    }
    case CmdTag::set_blend_factor: {
        SetBlendFactor c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_stencil_ref: {
        SetStencilRef c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_sample_positions: {
        SetSamplePositions c;
        if (!read_pod(ptr, end, c.sample_count)) return false;
        if (!read_pod(ptr, end, c.pixel_count)) return false;
        std::uint32_t pos_count;
        if (!read_pod(ptr, end, pos_count)) return false;
        if (pos_count > 64) return false;
        c.positions.resize(pos_count);
        for (std::uint32_t i = 0; i < pos_count; ++i) {
            if (!read_pod(ptr, end, c.positions[i])) return false;
        }
        out = std::move(c);
        return true;
    }
    case CmdTag::set_shading_rate: {
        SetShadingRate c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    case CmdTag::set_shading_rate_image: {
        SetShadingRateImage c;
        if (!read_pod(ptr, end, c)) return false;
        out = c;
        return true;
    }
    default:
        return false;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CommandList::serialize / deserialize
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> CommandList::serialize() const {
    std::vector<std::uint8_t> buf;
    // Reserve rough estimate: 64 bytes per command average
    buf.reserve(commands_.size() * 64 + 4);

    write_pod(buf, static_cast<std::uint32_t>(commands_.size()));
    for (const auto& cmd : commands_) {
        if (!serialize_command(buf, cmd)) return {};
    }
    return buf;
}

bool CommandList::deserialize(std::span<const std::uint8_t> data) {
    if (closed_) return false;

    const auto* ptr = data.data();
    const auto* end = ptr + data.size();

    std::uint32_t count;
    if (!read_pod(ptr, end, count)) return false;
    if (count > 1'000'000) return false;

    commands_.clear();
    commands_.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i) {
        Command cmd;
        if (!deserialize_command(ptr, end, cmd)) return false;
        commands_.push_back(std::move(cmd));
    }
    return true;
}

// ---------------------------------------------------------------------------
// CommandList::RingAllocator
// ---------------------------------------------------------------------------

CommandList::RingAllocator::RingAllocator(std::size_t capacity)
    : capacity_(capacity), write_pos_(0), read_pos_(0) {
    buffer_ = static_cast<Command*>(::operator new(sizeof(Command) * capacity));
}

CommandList::RingAllocator::~RingAllocator() {
    // Destroy any live Commands in the ring
    for (std::size_t i = read_pos_; i < write_pos_; ++i) {
        buffer_[i % capacity_].~Command();
    }
    ::operator delete(buffer_);
}

CommandList::RingAllocator::RingAllocator(RingAllocator&& other) noexcept
    : buffer_(other.buffer_), capacity_(other.capacity_),
      write_pos_(other.write_pos_), read_pos_(other.read_pos_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
    other.write_pos_ = 0;
    other.read_pos_ = 0;
}

CommandList::RingAllocator& CommandList::RingAllocator::operator=(RingAllocator&& other) noexcept {
    if (this != &other) {
        // Destroy current contents
        if (buffer_) {
            for (std::size_t i = read_pos_; i < write_pos_; ++i) {
                buffer_[i % capacity_].~Command();
            }
            ::operator delete(buffer_);
        }
        buffer_ = other.buffer_;
        capacity_ = other.capacity_;
        write_pos_ = other.write_pos_;
        read_pos_ = other.read_pos_;
        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.write_pos_ = 0;
        other.read_pos_ = 0;
    }
    return *this;
}

Command* CommandList::RingAllocator::allocate() {
    if (full()) return nullptr;
    std::size_t idx = write_pos_ % capacity_;
    // Placement-new a default-constructed Command (monostate initially)
    new (&buffer_[idx]) Command(TransitionBarrier{});
    ++write_pos_;
    return &buffer_[idx];
}

void CommandList::RingAllocator::reset() {
    for (std::size_t i = read_pos_; i < write_pos_; ++i) {
        buffer_[i % capacity_].~Command();
    }
    write_pos_ = 0;
    read_pos_ = 0;
}

std::span<const Command> CommandList::RingAllocator::commands() const {
    // Note: this only works correctly when the ring hasn't wrapped around.
    // For a true circular span we'd need two spans, but for per-frame usage
    // (reset each frame) the linear case is sufficient.
    if (write_pos_ <= capacity_) {
        return {buffer_, write_pos_};
    }
    // Wrapped: return from read_pos to end of buffer. Caller should handle wrap.
    return {buffer_ + read_pos_, capacity_ - read_pos_};
}

} // namespace dedsec