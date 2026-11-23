// DedSec PSO Pipeline — Graphics + Compute pipeline state creation with caching
// Hash-based cache to avoid recompilation, async creation on background threads,
// root signature binding and parameter mapping, XBCH parser integration.
// Original DedSec implementation; not copied from XCL or Leonida.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations — no d3d12.h in the header.
struct ID3D12Device;
struct ID3D12PipelineState;
struct ID3D12RootSignature;

namespace dedsec {

// Shader bytecode blob (DXIL or translated HLSL→DXIL).
struct ShaderBytecode {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

// Input layout element (mirrors D3D12_INPUT_ELEMENT_DESC without d3d12.h).
struct InputElement {
    const char* semantic_name;
    std::uint32_t semantic_index;
    std::uint32_t format;       // DXGI_FORMAT
    std::uint32_t input_slot;
    std::uint32_t aligned_byte_offset;
    std::uint32_t input_slot_class;  // 0 = per-vertex, 1 = per-instance
    std::uint32_t instance_data_step_rate;
};

// Blend state for one render target.
struct RenderTargetBlend {
    bool blend_enable{false};
    bool logic_op_enable{false};
    std::uint32_t src_blend{5};      // D3D12_BLEND_SRC_ALPHA
    std::uint32_t dest_blend{6};     // D3D12_BLEND_INV_SRC_ALPHA
    std::uint32_t blend_op{1};       // D3D12_BLEND_OP_ADD
    std::uint32_t src_blend_alpha{5};
    std::uint32_t dest_blend_alpha{6};
    std::uint32_t blend_op_alpha{1};
    std::uint8_t render_target_write_mask{0x0f};
};

// Depth-stencil state.
struct DepthStencilState {
    bool depth_enable{true};
    bool depth_write_mask{true};     // true = all, false = zero
    std::uint32_t depth_func{4};     // D3D12_COMPARISON_FUNC_LESS_EQUAL
    bool stencil_enable{false};
    std::uint8_t stencil_read_mask{0xff};
    std::uint8_t stencil_write_mask{0xff};
    // Front/back face ops simplified: fail_op, depth_fail_op, pass_op, func
    std::uint32_t front_face_ops[4]{1, 1, 1, 8};  // KEEP, KEEP, KEEP, ALWAYS
    std::uint32_t back_face_ops[4]{1, 1, 1, 8};
};

// Rasterizer state.
struct RasterizerState {
    std::uint32_t fill_mode{3};          // D3D12_FILL_MODE_SOLID
    std::uint32_t cull_mode{3};          // D3D12_CULL_MODE_BACK
    bool front_counter_clockwise{false};
    std::int32_t depth_bias{0};
    float depth_bias_clamp{0.0f};
    float slope_scaled_depth_bias{0.0f};
    bool depth_clip_enable{true};
    bool multisample_enable{false};
    bool antialiased_line_enable{false};
    std::uint32_t forced_sample_count{0};
    std::uint32_t conservative_raster{0}; // D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
};

// Graphics pipeline description — all stages + fixed-function state.
struct GraphicsPipelineDesc {
    ShaderBytecode vs;
    ShaderBytecode hs;   // hull shader (optional)
    ShaderBytecode ds;   // domain shader (optional)
    ShaderBytecode gs;   // geometry shader (optional)
    ShaderBytecode ps;   // pixel shader (optional)

    ID3D12RootSignature* root_signature{nullptr};

    std::vector<InputElement> input_elements;
    std::uint32_t input_layout_flags{0};  // D3D12_INPUT_LAYOUT_FLAGS

    std::uint32_t primitive_topology_type{3}; // D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE

    // Render target formats (up to 8). 0 = unused slot.
    std::uint32_t rtv_formats[8]{};
    std::uint32_t num_render_targets{1};
    std::uint32_t dsv_format{0};        // DXGI_FORMAT_UNKNOWN if no depth
    std::uint32_t sample_mask{0xffffffff};
    std::uint32_t sample_count{1};
    std::uint32_t sample_quality{0};

    // Fixed-function states
    RenderTargetBlend blend_states[8];
    bool alpha_to_coverage_enable{false};
    bool independent_blend_enable{false};
    DepthStencilState depth_stencil;
    RasterizerState rasterizer;

    // Stream output (optional)
    bool stream_output_enable{false};
};

// Compute pipeline description.
struct ComputePipelineDesc {
    ShaderBytecode cs;
    ID3D12RootSignature* root_signature{nullptr};
};

// Cache key: 64-bit hash of the pipeline description.
using PsoHash = std::uint64_t;

// Statistics.
struct PsoStats {
    std::uint64_t graphics_created{0};
    std::uint64_t compute_created{0};
    std::uint64_t cache_hits{0};
    std::uint64_t cache_misses{0};
    std::uint64_t async_submitted{0};
    std::uint64_t async_completed{0};
    std::uint64_t failures{0};
};

// Callback for async PSO completion.
using PsoReadyCallback = std::function<void(PsoHash hash, ID3D12PipelineState* pso)>;

// The PSO pipeline manager.
class PsoPipeline {
public:
    PsoPipeline();
    ~PsoPipeline();

    PsoPipeline(const PsoPipeline&) = delete;
    PsoPipeline& operator=(const PsoPipeline&) = delete;

    // Initialize with a PC D3D12 device. Must be called before any PSO creation.
    bool init(ID3D12Device* device);

    // Shutdown: stop workers, release cached PSOs.
    void shutdown();

    // --- Synchronous Creation ---

    // Create a graphics PSO. Returns nullptr on failure.
    // Checks cache first; on miss, creates synchronously and caches.
    ID3D12PipelineState* create_graphics(const GraphicsPipelineDesc& desc);

    // Create a compute PSO. Returns nullptr on failure.
    ID3D12PipelineState* create_compute(const ComputePipelineDesc& desc);

    // --- Asynchronous Creation ---

    // Submit a graphics PSO for background compilation. Returns the cache hash.
    // When ready, the callback is invoked (from a worker thread).
    PsoHash submit_graphics_async(const GraphicsPipelineDesc& desc, PsoReadyCallback cb = {});

    // Submit a compute PSO for background compilation.
    PsoHash submit_compute_async(const ComputePipelineDesc& desc, PsoReadyCallback cb = {});

    // Check if an async PSO is ready. Returns the PSO if done, nullptr otherwise.
    ID3D12PipelineState* query_async(PsoHash hash);

    // Block until an async PSO is ready. Returns the PSO or nullptr on failure.
    ID3D12PipelineState* wait_async(PsoHash hash);

    // --- Cache Management ---

    // Look up a cached PSO by hash. Returns nullptr if not found.
    ID3D12PipelineState* find(PsoHash hash) const;

    // Number of cached PSOs.
    std::size_t cache_size() const;

    // Clear the cache (releases all PSOs).
    void clear_cache();

    // --- Statistics ---

    PsoStats stats() const;
    void reset_stats();

    // --- Hash Computation ---

    // Compute the hash for a graphics pipeline description.
    static PsoHash hash_graphics(const GraphicsPipelineDesc& desc);

    // Compute the hash for a compute pipeline description.
    static PsoHash hash_compute(const ComputePipelineDesc& desc);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace dedsec