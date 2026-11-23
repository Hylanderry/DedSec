// DedSec PSO Pipeline — Graphics + Compute pipeline state creation with caching
// Hash-based cache to avoid recompilation, async creation on background threads,
// root signature binding and parameter mapping, XBCH parser integration.
// Original DedSec implementation; not copied from XCL or Leonida.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "dedsec_pso_pipeline.h"

#ifdef _WIN32
#include <windows.h>
#include <d3d12.h>
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dedsec {

// ---------------------------------------------------------------------------
// FNV-1a 64-bit hash for pipeline descriptions
// ---------------------------------------------------------------------------
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv1a(const void* data, std::size_t len) {
    auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = kFnvOffsetBasis;
    for (std::size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= kFnvPrime;
    }
    return h;
}

std::uint64_t fnv1a_combine(std::uint64_t h, const void* data, std::size_t len) {
    auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= kFnvPrime;
    }
    return h;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct PsoPipeline::Impl {
    ID3D12Device* device{nullptr};

    // Cache: hash → PSO pointer (raw, refcount managed by us)
    mutable std::shared_mutex cache_mutex;
    std::unordered_map<PsoHash, ID3D12PipelineState*> cache;

    // Async work queue
    struct AsyncJob {
        PsoHash hash;
        bool is_compute;
        // For graphics: serialized desc components needed for creation
        GraphicsPipelineDesc gfx_desc;
        ComputePipelineDesc comp_desc;
        PsoReadyCallback callback;
    };

    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<AsyncJob> work_queue;
    bool shutdown_flag{false};
    std::vector<std::thread> workers;

    // Pending async results: hash → (pso, done)
    struct PendingResult {
        ID3D12PipelineState* pso{nullptr};
        bool done{false};
        std::mutex mtx;
        std::condition_variable cv;
    };
    std::mutex pending_mutex;
    std::unordered_map<PsoHash, std::shared_ptr<PendingResult>> pending;

    // Stats
    mutable std::mutex stats_mutex;
    PsoStats stats;

    void report_stat_graphics() {
        std::lock_guard lock(stats_mutex);
        stats.graphics_created++;
    }
    void report_stat_compute() {
        std::lock_guard lock(stats_mutex);
        stats.compute_created++;
    }
    void report_stat_cache_hit() {
        std::lock_guard lock(stats_mutex);
        stats.cache_hits++;
    }
    void report_stat_cache_miss() {
        std::lock_guard lock(stats_mutex);
        stats.cache_misses++;
    }
    void report_stat_async_submitted() {
        std::lock_guard lock(stats_mutex);
        stats.async_submitted++;
    }
    void report_stat_async_completed() {
        std::lock_guard lock(stats_mutex);
        stats.async_completed++;
    }
    void report_stat_failure() {
        std::lock_guard lock(stats_mutex);
        stats.failures++;
    }

    // Worker thread function
    void worker_func() {
        while (true) {
            AsyncJob job;
            {
                std::unique_lock lock(queue_mutex);
                queue_cv.wait(lock, [this] { return !work_queue.empty() || shutdown_flag; });
                if (shutdown_flag && work_queue.empty()) return;
                job = std::move(work_queue.front());
                work_queue.pop();
            }

            ID3D12PipelineState* pso = nullptr;
            if (job.is_compute) {
                pso = create_compute_internal(job.comp_desc);
            } else {
                pso = create_graphics_internal(job.gfx_desc);
            }

            // Store in cache
            if (pso) {
                {
                    std::unique_lock lock(cache_mutex);
                    cache[job.hash] = pso;
                }
                report_stat_async_completed();
            } else {
                report_stat_failure();
            }

            // Signal pending result
            {
                std::lock_guard lock(pending_mutex);
                auto it = pending.find(job.hash);
                if (it != pending.end()) {
                    std::lock_guard plock(it->second->mtx);
                    it->second->pso = pso;
                    it->second->done = true;
                    it->second->cv.notify_all();
                }
            }

            // Invoke callback
            if (job.callback) {
                job.callback(job.hash, pso);
            }
        }
    }

    void start_workers() {
        int n = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);
        if (n > 6) n = 6; // cap at 6 workers (DXC can be memory-heavy)
        for (int i = 0; i < n; ++i) {
            workers.emplace_back(&Impl::worker_func, this);
        }
    }

    void stop_workers() {
        {
            std::lock_guard lock(queue_mutex);
            shutdown_flag = true;
        }
        queue_cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
        workers.clear();
    }

#ifdef _WIN32
    ID3D12PipelineState* create_graphics_internal(const GraphicsPipelineDesc& desc) {
        if (!device) return nullptr;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = desc.root_signature;

        // Shaders
        if (desc.vs.data && desc.vs.size) {
            pso_desc.VS.pShaderBytecode = desc.vs.data;
            pso_desc.VS.BytecodeLength = desc.vs.size;
        }
        if (desc.hs.data && desc.hs.size) {
            pso_desc.HS.pShaderBytecode = desc.hs.data;
            pso_desc.HS.BytecodeLength = desc.hs.size;
        }
        if (desc.ds.data && desc.ds.size) {
            pso_desc.DS.pShaderBytecode = desc.ds.data;
            pso_desc.DS.BytecodeLength = desc.ds.size;
        }
        if (desc.gs.data && desc.gs.size) {
            pso_desc.GS.pShaderBytecode = desc.gs.data;
            pso_desc.GS.BytecodeLength = desc.gs.size;
        }
        if (desc.ps.data && desc.ps.size) {
            pso_desc.PS.pShaderBytecode = desc.ps.data;
            pso_desc.PS.BytecodeLength = desc.ps.size;
        }

        // Input layout
        std::vector<D3D12_INPUT_ELEMENT_DESC> ie_descs;
        ie_descs.reserve(desc.input_elements.size());
        for (const auto& ie : desc.input_elements) {
            D3D12_INPUT_ELEMENT_DESC d{};
            d.SemanticName = ie.semantic_name;
            d.SemanticIndex = ie.semantic_index;
            d.Format = static_cast<DXGI_FORMAT>(ie.format);
            d.InputSlot = ie.input_slot;
            d.AlignedByteOffset = ie.aligned_byte_offset;
            d.InputSlotClass = static_cast<D3D12_INPUT_CLASSIFICATION>(ie.input_slot_class);
            d.InstanceDataStepRate = ie.instance_data_step_rate;
            ie_descs.push_back(d);
        }
        pso_desc.InputLayout.pInputElementDescs = ie_descs.empty() ? nullptr : ie_descs.data();
        pso_desc.InputLayout.NumElements = static_cast<UINT>(ie_descs.size());

        // Primitive topology
        pso_desc.PrimitiveTopologyType = static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(desc.primitive_topology_type);

        // Render targets
        pso_desc.NumRenderTargets = desc.num_render_targets;
        for (std::uint32_t i = 0; i < desc.num_render_targets && i < 8; ++i) {
            pso_desc.RTVFormats[i] = static_cast<DXGI_FORMAT>(desc.rtv_formats[i]);
        }
        pso_desc.DSVFormat = static_cast<DXGI_FORMAT>(desc.dsv_format);

        // Sample
        pso_desc.SampleMask = desc.sample_mask;
        pso_desc.SampleDesc.Count = desc.sample_count;
        pso_desc.SampleDesc.Quality = desc.sample_quality;

        // Blend state
        pso_desc.BlendState.AlphaToCoverageEnable = desc.alpha_to_coverage_enable ? TRUE : FALSE;
        pso_desc.BlendState.IndependentBlendEnable = desc.independent_blend_enable ? TRUE : FALSE;
        for (int i = 0; i < 8; ++i) {
            auto& rt = pso_desc.BlendState.RenderTarget[i];
            rt.BlendEnable = desc.blend_states[i].blend_enable ? TRUE : FALSE;
            rt.LogicOpEnable = desc.blend_states[i].logic_op_enable ? TRUE : FALSE;
            rt.SrcBlend = static_cast<D3D12_BLEND>(desc.blend_states[i].src_blend);
            rt.DestBlend = static_cast<D3D12_BLEND>(desc.blend_states[i].dest_blend);
            rt.BlendOp = static_cast<D3D12_BLEND_OP>(desc.blend_states[i].blend_op);
            rt.SrcBlendAlpha = static_cast<D3D12_BLEND>(desc.blend_states[i].src_blend_alpha);
            rt.DestBlendAlpha = static_cast<D3D12_BLEND>(desc.blend_states[i].dest_blend_alpha);
            rt.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(desc.blend_states[i].blend_op_alpha);
            rt.RenderTargetWriteMask = desc.blend_states[i].render_target_write_mask;
        }

        // Depth-stencil
        pso_desc.DepthStencilState.DepthEnable = desc.depth_stencil.depth_enable ? TRUE : FALSE;
        pso_desc.DepthStencilState.DepthWriteMask = desc.depth_stencil.depth_write_mask
                                                        ? D3D12_DEPTH_WRITE_MASK_ALL
                                                        : D3D12_DEPTH_WRITE_MASK_ZERO;
        pso_desc.DepthStencilState.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(desc.depth_stencil.depth_func);
        pso_desc.DepthStencilState.StencilEnable = desc.depth_stencil.stencil_enable ? TRUE : FALSE;
        pso_desc.DepthStencilState.StencilReadMask = desc.depth_stencil.stencil_read_mask;
        pso_desc.DepthStencilState.StencilWriteMask = desc.depth_stencil.stencil_write_mask;
        pso_desc.DepthStencilState.FrontFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(desc.depth_stencil.front_face_ops[0]);
        pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = static_cast<D3D12_STENCIL_OP>(desc.depth_stencil.front_face_ops[1]);
        pso_desc.DepthStencilState.FrontFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(desc.depth_stencil.front_face_ops[2]);
        pso_desc.DepthStencilState.FrontFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(desc.depth_stencil.front_face_ops[3]);
        pso_desc.DepthStencilState.BackFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(desc.depth_stencil.back_face_ops[0]);
        pso_desc.DepthStencilState.BackFace.StencilDepthFailOp = static_cast<D3D12_STENCIL_OP>(desc.depth_stencil.back_face_ops[1]);
        pso_desc.DepthStencilState.BackFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(desc.depth_stencil.back_face_ops[2]);
        pso_desc.DepthStencilState.BackFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(desc.depth_stencil.back_face_ops[3]);

        // Rasterizer
        pso_desc.RasterizerState.FillMode = static_cast<D3D12_FILL_MODE>(desc.rasterizer.fill_mode);
        pso_desc.RasterizerState.CullMode = static_cast<D3D12_CULL_MODE>(desc.rasterizer.cull_mode);
        pso_desc.RasterizerState.FrontCounterClockwise = desc.rasterizer.front_counter_clockwise ? TRUE : FALSE;
        pso_desc.RasterizerState.DepthBias = desc.rasterizer.depth_bias;
        pso_desc.RasterizerState.DepthBiasClamp = desc.rasterizer.depth_bias_clamp;
        pso_desc.RasterizerState.SlopeScaledDepthBias = desc.rasterizer.slope_scaled_depth_bias;
        pso_desc.RasterizerState.DepthClipEnable = desc.rasterizer.depth_clip_enable ? TRUE : FALSE;
        pso_desc.RasterizerState.MultisampleEnable = desc.rasterizer.multisample_enable ? TRUE : FALSE;
        pso_desc.RasterizerState.AntialiasedLineEnable = desc.rasterizer.antialiased_line_enable ? TRUE : FALSE;
        pso_desc.RasterizerState.ForcedSampleCount = desc.rasterizer.forced_sample_count;
        pso_desc.RasterizerState.ConservativeRaster = static_cast<D3D12_CONSERVATIVE_RASTERIZATION_MODE>(desc.rasterizer.conservative_raster);

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
        if (FAILED(hr)) return nullptr;

        report_stat_graphics();
        return pso;
    }

    ID3D12PipelineState* create_compute_internal(const ComputePipelineDesc& desc) {
        if (!device) return nullptr;

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = desc.root_signature;
        if (desc.cs.data && desc.cs.size) {
            pso_desc.CS.pShaderBytecode = desc.cs.data;
            pso_desc.CS.BytecodeLength = desc.cs.size;
        }

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
        if (FAILED(hr)) return nullptr;

        report_stat_compute();
        return pso;
    }
#else
    ID3D12PipelineState* create_graphics_internal(const GraphicsPipelineDesc&) { return nullptr; }
    ID3D12PipelineState* create_compute_internal(const ComputePipelineDesc&) { return nullptr; }
#endif
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

PsoPipeline::PsoPipeline() : impl_(new Impl()) {}

PsoPipeline::~PsoPipeline() {
    shutdown();
    delete impl_;
}

bool PsoPipeline::init(ID3D12Device* device) {
#ifdef _WIN32
    if (!device) return false;
    impl_->device = device;
    impl_->start_workers();
    return true;
#else
    (void)device;
    return false;
#endif
}

void PsoPipeline::shutdown() {
    impl_->stop_workers();

    // Release all cached PSOs
    {
        std::unique_lock lock(impl_->cache_mutex);
        for (auto& [hash, pso] : impl_->cache) {
            if (pso) pso->Release();
        }
        impl_->cache.clear();
    }
}

ID3D12PipelineState* PsoPipeline::create_graphics(const GraphicsPipelineDesc& desc) {
    PsoHash h = hash_graphics(desc);

    // Check cache
    {
        std::shared_lock lock(impl_->cache_mutex);
        auto it = impl_->cache.find(h);
        if (it != impl_->cache.end()) {
            impl_->report_stat_cache_hit();
            return it->second;
        }
    }

    impl_->report_stat_cache_miss();

    // Create synchronously
    auto* pso = impl_->create_graphics_internal(desc);
    if (pso) {
        std::unique_lock lock(impl_->cache_mutex);
        // Double-check: another thread may have created it
        auto [it, inserted] = impl_->cache.emplace(h, pso);
        if (!inserted) {
            // Already exists, release ours
            pso->Release();
            pso = it->second;
        }
    }
    return pso;
}

ID3D12PipelineState* PsoPipeline::create_compute(const ComputePipelineDesc& desc) {
    PsoHash h = hash_compute(desc);

    {
        std::shared_lock lock(impl_->cache_mutex);
        auto it = impl_->cache.find(h);
        if (it != impl_->cache.end()) {
            impl_->report_stat_cache_hit();
            return it->second;
        }
    }

    impl_->report_stat_cache_miss();

    auto* pso = impl_->create_compute_internal(desc);
    if (pso) {
        std::unique_lock lock(impl_->cache_mutex);
        auto [it, inserted] = impl_->cache.emplace(h, pso);
        if (!inserted) {
            pso->Release();
            pso = it->second;
        }
    }
    return pso;
}

PsoHash PsoPipeline::submit_graphics_async(const GraphicsPipelineDesc& desc, PsoReadyCallback cb) {
    PsoHash h = hash_graphics(desc);

    // Check cache first
    {
        std::shared_lock lock(impl_->cache_mutex);
        auto it = impl_->cache.find(h);
        if (it != impl_->cache.end()) {
            impl_->report_stat_cache_hit();
            if (cb) cb(h, it->second);
            return h;
        }
    }

    // Create pending result
    auto pr = std::make_shared<Impl::PendingResult>();
    {
        std::lock_guard lock(impl_->pending_mutex);
        impl_->pending[h] = pr;
    }

    // Enqueue
    {
        std::lock_guard lock(impl_->queue_mutex);
        impl_->work_queue.push({h, false, desc, {}, std::move(cb)});
    }
    impl_->queue_cv.notify_one();
    impl_->report_stat_async_submitted();

    return h;
}

PsoHash PsoPipeline::submit_compute_async(const ComputePipelineDesc& desc, PsoReadyCallback cb) {
    PsoHash h = hash_compute(desc);

    {
        std::shared_lock lock(impl_->cache_mutex);
        auto it = impl_->cache.find(h);
        if (it != impl_->cache.end()) {
            impl_->report_stat_cache_hit();
            if (cb) cb(h, it->second);
            return h;
        }
    }

    auto pr = std::make_shared<Impl::PendingResult>();
    {
        std::lock_guard lock(impl_->pending_mutex);
        impl_->pending[h] = pr;
    }

    {
        std::lock_guard lock(impl_->queue_mutex);
        impl_->work_queue.push({h, true, {}, desc, std::move(cb)});
    }
    impl_->queue_cv.notify_one();
    impl_->report_stat_async_submitted();

    return h;
}

ID3D12PipelineState* PsoPipeline::query_async(PsoHash hash) {
    // Check cache
    {
        std::shared_lock lock(impl_->cache_mutex);
        auto it = impl_->cache.find(hash);
        if (it != impl_->cache.end()) return it->second;
    }

    // Check pending
    std::shared_ptr<Impl::PendingResult> pr;
    {
        std::lock_guard lock(impl_->pending_mutex);
        auto it = impl_->pending.find(hash);
        if (it == impl_->pending.end()) return nullptr;
        pr = it->second;
    }

    std::lock_guard lock(pr->mtx);
    return pr->done ? pr->pso : nullptr;
}

ID3D12PipelineState* PsoPipeline::wait_async(PsoHash hash) {
    // Check cache first
    {
        std::shared_lock lock(impl_->cache_mutex);
        auto it = impl_->cache.find(hash);
        if (it != impl_->cache.end()) return it->second;
    }

    std::shared_ptr<Impl::PendingResult> pr;
    {
        std::lock_guard lock(impl_->pending_mutex);
        auto it = impl_->pending.find(hash);
        if (it == impl_->pending.end()) return nullptr;
        pr = it->second;
    }

    {
        std::unique_lock lock(pr->mtx);
        pr->cv.wait(lock, [&] { return pr->done; });
    }

    return pr->pso;
}

ID3D12PipelineState* PsoPipeline::find(PsoHash hash) const {
    std::shared_lock lock(impl_->cache_mutex);
    auto it = impl_->cache.find(hash);
    return (it != impl_->cache.end()) ? it->second : nullptr;
}

std::size_t PsoPipeline::cache_size() const {
    std::shared_lock lock(impl_->cache_mutex);
    return impl_->cache.size();
}

void PsoPipeline::clear_cache() {
    std::unique_lock lock(impl_->cache_mutex);
    for (auto& [hash, pso] : impl_->cache) {
        if (pso) pso->Release();
    }
    impl_->cache.clear();
}

PsoStats PsoPipeline::stats() const {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->stats;
}

void PsoPipeline::reset_stats() {
    std::lock_guard lock(impl_->stats_mutex);
    impl_->stats = {};
}

PsoHash PsoPipeline::hash_graphics(const GraphicsPipelineDesc& desc) {
    std::uint64_t h = kFnvOffsetBasis;

    // Hash shader bytecodes
    if (desc.vs.data && desc.vs.size) h = fnv1a_combine(h, desc.vs.data, desc.vs.size);
    if (desc.hs.data && desc.hs.size) h = fnv1a_combine(h, desc.hs.data, desc.hs.size);
    if (desc.ds.data && desc.ds.size) h = fnv1a_combine(h, desc.ds.data, desc.ds.size);
    if (desc.gs.data && desc.gs.size) h = fnv1a_combine(h, desc.gs.data, desc.gs.size);
    if (desc.ps.data && desc.ps.size) h = fnv1a_combine(h, desc.ps.data, desc.ps.size);

    // Hash root signature pointer (identity)
    h = fnv1a_combine(h, &desc.root_signature, sizeof(desc.root_signature));

    // Hash input layout
    for (const auto& ie : desc.input_elements) {
        if (ie.semantic_name) h = fnv1a_combine(h, ie.semantic_name, std::strlen(ie.semantic_name));
        h = fnv1a_combine(h, &ie.semantic_index, sizeof(ie.semantic_index));
        h = fnv1a_combine(h, &ie.format, sizeof(ie.format));
        h = fnv1a_combine(h, &ie.input_slot, sizeof(ie.input_slot));
        h = fnv1a_combine(h, &ie.aligned_byte_offset, sizeof(ie.aligned_byte_offset));
    }

    // Hash fixed-function state
    h = fnv1a_combine(h, &desc.primitive_topology_type, sizeof(desc.primitive_topology_type));
    h = fnv1a_combine(h, desc.rtv_formats, sizeof(desc.rtv_formats));
    h = fnv1a_combine(h, &desc.num_render_targets, sizeof(desc.num_render_targets));
    h = fnv1a_combine(h, &desc.dsv_format, sizeof(desc.dsv_format));
    h = fnv1a_combine(h, &desc.sample_mask, sizeof(desc.sample_mask));
    h = fnv1a_combine(h, &desc.sample_count, sizeof(desc.sample_count));

    // Hash blend, depth-stencil, rasterizer states
    h = fnv1a_combine(h, desc.blend_states, sizeof(desc.blend_states));
    h = fnv1a_combine(h, &desc.alpha_to_coverage_enable, sizeof(desc.alpha_to_coverage_enable));
    h = fnv1a_combine(h, &desc.independent_blend_enable, sizeof(desc.independent_blend_enable));
    h = fnv1a_combine(h, &desc.depth_stencil, sizeof(desc.depth_stencil));
    h = fnv1a_combine(h, &desc.rasterizer, sizeof(desc.rasterizer));

    return h;
}

PsoHash PsoPipeline::hash_compute(const ComputePipelineDesc& desc) {
    std::uint64_t h = kFnvOffsetBasis;
    if (desc.cs.data && desc.cs.size) h = fnv1a_combine(h, desc.cs.data, desc.cs.size);
    h = fnv1a_combine(h, &desc.root_signature, sizeof(desc.root_signature));
    return h;
}

} // namespace dedsec