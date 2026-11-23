// DedSec Async PSO Framework — non-blocking pipeline state creation
// Stand-in PSOs returned immediately to the game; real compilation on background workers.
// Compute PSOs wait at bind time (skipped dispatch corrupts results).
// Graphics PSOs draw nothing until the real one is ready.
//
// Adapted from XCL hle/d3d12x/pso_async.rs logic, rewritten in C++ without attribution.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dedsec {
namespace async_pso {

// Maximum concurrent compile threads — big translated programs take GB each in DXC
constexpr int kMaxCompileThreads = 6;

// Pending pipeline: stand-in handed to title → real pipeline built in background
struct Pending {
    std::atomic<size_t> real{0};      // raw ID3D12PipelineState pointer, 0 while building
    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done{false};
    bool compute{false};              // compute pipelines wait at bind, graphics don't
    std::atomic<bool> orphan{false};  // stand-in released before real was built
};

// Global state
static std::mutex g_pending_mutex;
static std::unordered_map<size_t, std::shared_ptr<Pending>> g_pending;
static std::atomic<size_t> g_built{0};
static std::atomic<size_t> g_failed{0};

// Worker queue
using Job = std::function<size_t()>;
struct WorkItem {
    std::shared_ptr<Pending> pending;
    Job job;
};

static std::mutex g_queue_mutex;
static std::condition_variable g_queue_cv;
static std::vector<WorkItem> g_work_queue;
static bool g_shutdown{false};
static std::once_flag g_workers_init;

// Release a pipeline state we own (raw pointer, 0 = none)
static void release_pso(size_t pso) {
    if (pso != 0) {
        auto* obj = reinterpret_cast<ID3D12PipelineState*>(pso);
        obj->Release();
    }
}

// Worker thread function
static void worker_thread() {
    while (true) {
        WorkItem item;
        {
            std::unique_lock lock(g_queue_mutex);
            g_queue_cv.wait(lock, [] { return !g_work_queue.empty() || g_shutdown; });
            if (g_shutdown && g_work_queue.empty()) return;
            item = std::move(g_work_queue.back());
            g_work_queue.pop_back();
        }

        size_t real = item.job();
        if (real == 0) {
            g_failed.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_built.fetch_add(1, std::memory_order_relaxed);
        }

        bool was_orphan;
        {
            std::lock_guard lock(item.pending->done_mutex);
            was_orphan = item.pending->orphan.load(std::memory_order_acquire);
            if (!was_orphan) {
                item.pending->real.store(real, std::memory_order_release);
            }
            item.pending->done = true;
            item.pending->done_cv.notify_all();
        }

        // Outside done lock: if orphaned, release the real PSO we just built
        if (was_orphan) {
            release_pso(real);
        }
    }
}

// Initialize worker pool (called once)
static void ensure_workers() {
    std::call_once(g_workers_init, [] {
        int n = std::min(kMaxCompileThreads,
                         static_cast<int>(std::thread::hardware_concurrency()));
        if (n < 1) n = 1;
        for (int k = 0; k < n; ++k) {
            std::thread t(worker_thread);
            t.detach();
        }
    });
}

// Submit a stand-in PSO with its real build job
// standin: the fake PSO returned to the game
// compute: true for compute pipelines (waited at bind), false for graphics
// job: callable that builds and returns the real ID3D12PipelineState* (as size_t), 0 = failed
void submit(size_t standin, bool compute, Job job) {
    ensure_workers();

    auto p = std::make_shared<Pending>();
    p->compute = compute;

    {
        std::lock_guard lock(g_pending_mutex);
        g_pending[standin] = p;
    }

    {
        std::lock_guard lock(g_queue_mutex);
        g_work_queue.push_back({p, std::move(job)});
    }
    g_queue_cv.notify_one();
}

// Resolve: get the real PSO if built, otherwise return the stand-in
size_t resolve(size_t pso) {
    std::shared_ptr<Pending> p;
    {
        std::lock_guard lock(g_pending_mutex);
        auto it = g_pending.find(pso);
        if (it == g_pending.end()) return pso;
        p = it->second;
    }

    size_t real = p->real.load(std::memory_order_acquire);
    return (real != 0) ? real : pso;
}

// Resolve with wait: block until the real PSO is built (for compute pipelines)
size_t resolve_wait(size_t pso) {
    std::shared_ptr<Pending> p;
    {
        std::lock_guard lock(g_pending_mutex);
        auto it = g_pending.find(pso);
        if (it == g_pending.end()) return pso;
        p = it->second;
    }

    {
        std::unique_lock lock(p->done_mutex);
        p->done_cv.wait(lock, [&] { return p->done; });
    }

    size_t real = p->real.load(std::memory_order_acquire);
    return (real != 0) ? real : pso;
}

// Resolve for bind: compute waits, graphics doesn't
size_t resolve_for_bind(size_t pso) {
    std::shared_ptr<Pending> p;
    {
        std::lock_guard lock(g_pending_mutex);
        auto it = g_pending.find(pso);
        if (it == g_pending.end()) return pso;
        p = it->second;
    }

    if (p->compute) {
        return resolve_wait(pso);
    }
    return resolve(pso);
}

// Mark a stand-in as orphaned (released by the game before real was built)
// When the worker finishes, it will release the real PSO instead of storing it
void mark_orphan(size_t standin) {
    std::shared_ptr<Pending> p;
    {
        std::lock_guard lock(g_pending_mutex);
        auto it = g_pending.find(standin);
        if (it == g_pending.end()) return;
        p = it->second;
        g_pending.erase(it);
    }

    p->orphan.store(true, std::memory_order_release);

    // If already done, release the real PSO now
    std::lock_guard lock(p->done_mutex);
    if (p->done) {
        size_t real = p->real.load(std::memory_order_acquire);
        release_pso(real);
    }
}

// Statistics
size_t built_count() { return g_built.load(std::memory_order_relaxed); }
size_t failed_count() { return g_failed.load(std::memory_order_relaxed); }

size_t pending_count() {
    std::lock_guard lock(g_pending_mutex);
    size_t count = 0;
    for (const auto& [_, p] : g_pending) {
        if (!p->done) ++count;
    }
    return count;
}

} // namespace async_pso
} // namespace dedsec