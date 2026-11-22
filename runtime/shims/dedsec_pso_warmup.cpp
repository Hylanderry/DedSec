// DedSec Fork — DX12 Native Pipeline
// PSO Pre-warming System — Implementation
// Built from scratch for the DedSec DX12 native pipeline.
//
// Background thread pool compiles PSOs during loading screens.
// Uses condition variable for efficient wake/sleep and atomic stats.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_PSO_WARMUP_IMPLEMENTATION
#include "dedsec_pso_warmup.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>

namespace dedsec {

// ─── Static state ─────────────────────────────────────────────────────────────
ID3D12Device* PsoWarmupSystem::s_device = nullptr;
std::vector<std::thread> PsoWarmupSystem::s_workers;
std::queue<PsoWarmupItem> PsoWarmupSystem::s_work_queue;
std::mutex PsoWarmupSystem::s_queue_mutex;
std::condition_variable PsoWarmupSystem::s_queue_cv;
std::atomic<bool> PsoWarmupSystem::s_shutdown_flag{false};
std::atomic<bool> PsoWarmupSystem::s_loading_screen_active{false};
std::unordered_map<uint64_t, ID3D12PipelineState*> PsoWarmupSystem::s_pso_cache;
std::mutex PsoWarmupSystem::s_cache_mutex;
std::atomic<uint32_t> PsoWarmupSystem::s_pending{0};
std::atomic<uint32_t> PsoWarmupSystem::s_compiled{0};
std::atomic<uint32_t> PsoWarmupSystem::s_hits{0};
std::atomic<uint32_t> PsoWarmupSystem::s_misses{0};
bool PsoWarmupSystem::s_initialized = false;

// ─── Hash function for PsoKey ─────────────────────────────────────────────────
uint64_t PsoWarmupSystem::pso_key_hash(const PsoKey& key) {
    // FNV-1a over all fields
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&h](const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<uint64_t>(p[i]);
            h *= 0x100000001B3ULL;
        }
    };
    mix(&key.vs_hash, 8);
    mix(&key.ps_hash, 8);
    mix(&key.cs_hash, 8);
    mix(&key.blend_state, 4);
    mix(&key.rasterizer, 4);
    mix(&key.depth_stencil, 4);
    mix(&key.rtv_format, 4);
    mix(&key.dsv_format, 4);
    mix(&key.sample_mask, 1);
    mix(&key.topology, 1);
    return h;
}

// ─── Worker thread ────────────────────────────────────────────────────────────
void PsoWarmupSystem::worker_thread_func(uint32_t thread_id) {
    std::printf("[DedSec PSO] Worker %u started\n", thread_id);

    while (!s_shutdown_flag.load()) {
        PsoWarmupItem item;

        {
            std::unique_lock lock(s_queue_mutex);
            // Wait with timeout so we can check shutdown flag periodically
            s_queue_cv.wait_for(lock, std::chrono::milliseconds(100), [] {
                return !s_work_queue.empty() || s_shutdown_flag.load();
            });

            if (s_shutdown_flag.load() && s_work_queue.empty()) break;
            if (s_work_queue.empty()) continue;

            item = s_work_queue.front();
            s_work_queue.pop();
        }

        // Compile the PSO
        ID3D12PipelineState* pso = nullptr;
        HRESULT hr;

        if (item.is_compute) {
            hr = s_device->CreateComputePipelineState(&item.cs_desc, IID_PPV_ARGS(&pso));
        } else {
            hr = s_device->CreateGraphicsPipelineState(&item.gfx_desc, IID_PPV_ARGS(&pso));
        }

        if (SUCCEEDED(hr) && pso != nullptr) {
            uint64_t hash = pso_key_hash(item.key);
            {
                std::lock_guard lock(s_cache_mutex);
                // Check if another thread already compiled this PSO
                auto it = s_pso_cache.find(hash);
                if (it == s_pso_cache.end()) {
                    s_pso_cache[hash] = pso;
                } else {
                    // Duplicate — release ours
                    pso->Release();
                }
            }
            s_compiled.fetch_add(1);
        } else {
            std::fprintf(stderr, "[DedSec PSO] Worker %u: compilation failed (hr=0x%08x)\n",
                         thread_id, static_cast<unsigned>(hr));
        }

        s_pending.fetch_sub(1);
    }

    std::printf("[DedSec PSO] Worker %u exiting\n", thread_id);
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool PsoWarmupSystem::initialize(ID3D12Device* device, uint32_t num_threads) {
    if (s_initialized) return true;
    if (!device) return false;

    s_device = device;
    s_device->AddRef();
    s_shutdown_flag.store(false);

    if (num_threads == 0) {
        num_threads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }
    // Cap at 8 workers to avoid overwhelming the system
    num_threads = std::min(num_threads, 8u);

    s_workers.reserve(num_threads);
    for (uint32_t i = 0; i < num_threads; ++i) {
        s_workers.emplace_back(worker_thread_func, i);
    }

    s_initialized = true;
    std::printf("[DedSec PSO] Initialized with %u worker threads\n", num_threads);
    return true;
}

void PsoWarmupSystem::shutdown() {
    if (!s_initialized) return;

    s_shutdown_flag.store(true);
    s_queue_cv.notify_all();

    for (auto& w : s_workers) {
        if (w.joinable()) w.join();
    }
    s_workers.clear();

    // Release all cached PSOs
    {
        std::lock_guard lock(s_cache_mutex);
        for (auto& [hash, pso] : s_pso_cache) {
            if (pso) pso->Release();
        }
        s_pso_cache.clear();
    }

    if (s_device) {
        s_device->Release();
        s_device = nullptr;
    }

    s_initialized = false;
    std::printf("[DedSec PSO] Shutdown: %u compiled, %u hits, %u misses\n",
                s_compiled.load(), s_hits.load(), s_misses.load());
}

void PsoWarmupSystem::enqueue(const PsoWarmupItem& item) {
    if (!s_initialized) return;

    uint64_t hash = pso_key_hash(item.key);

    // Check if already cached
    {
        std::lock_guard lock(s_cache_mutex);
        if (s_pso_cache.count(hash)) return;
    }

    {
        std::lock_guard lock(s_queue_mutex);
        s_work_queue.push(item);
    }
    s_pending.fetch_add(1);
    s_queue_cv.notify_one();
}

void PsoWarmupSystem::enqueue_batch(const std::vector<PsoWarmupItem>& items) {
    if (!s_initialized || items.empty()) return;

    {
        std::lock_guard lock(s_queue_mutex);
        for (const auto& item : items) {
            uint64_t hash = pso_key_hash(item.key);
            // Skip if already cached
            {
                std::lock_guard cache_lock(s_cache_mutex);
                if (s_pso_cache.count(hash)) continue;
            }
            s_work_queue.push(item);
            s_pending.fetch_add(1);
        }
    }
    s_queue_cv.notify_all();
}

ID3D12PipelineState* PsoWarmupSystem::try_get(const PsoKey& key) {
    if (!s_initialized) return nullptr;

    uint64_t hash = pso_key_hash(key);
    std::lock_guard lock(s_cache_mutex);
    auto it = s_pso_cache.find(hash);
    if (it != s_pso_cache.end()) {
        s_hits.fetch_add(1);
        return it->second;
    }
    s_misses.fetch_add(1);
    return nullptr;
}

ID3D12PipelineState* PsoWarmupSystem::wait_for(const PsoKey& key, uint32_t timeout_ms) {
    if (!s_initialized) return nullptr;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto* pso = try_get(key);
        if (pso) return pso;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return nullptr;
}

void PsoWarmupSystem::on_loading_screen_start() {
    s_loading_screen_active.store(true);
    // Wake all workers to process queued items faster
    s_queue_cv.notify_all();
}

void PsoWarmupSystem::on_loading_screen_end() {
    s_loading_screen_active.store(false);
}

uint32_t PsoWarmupSystem::get_pending_count() { return s_pending.load(); }
uint32_t PsoWarmupSystem::get_compiled_count() { return s_compiled.load(); }
uint32_t PsoWarmupSystem::get_cache_hit_count() { return s_hits.load(); }
uint32_t PsoWarmupSystem::get_cache_miss_count() { return s_misses.load(); }

} // namespace dedsec