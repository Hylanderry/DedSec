// DedSec CPU Writes Bottom-of-Pipe — original implementation.
// See dedsec_cpu_writes.h for design notes and environment variables.
#include "dedsec_cpu_writes.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace dedsec::cpu_writes {
namespace {

// ---- Ring buffer entry ----

struct WriteBatch {
    uintptr_t va;
    uint32_t dwords[kMaxBatchDwords];
    size_t count;
    ID3D12Fence* fence;
    uint64_t fence_value;
};

// ---- Shared state ----

std::mutex g_mutex;
std::condition_variable g_cv;
WriteBatch g_ring[kRingCapacity];
size_t g_head = 0;   // next slot the worker reads
size_t g_tail = 0;   // next slot the submitter writes
size_t g_count = 0;  // number of occupied slots
std::atomic<bool> g_running{false};
std::thread g_worker;

// Per-queue fence tracking: queue pointer -> (fence, last_signaled_value).
// Fences are created lazily on first enqueue for each queue and released at shutdown.
struct QueueFence {
    ID3D12Fence* fence = nullptr;
    uint64_t last_value = 0;
};
std::unordered_map<ID3D12CommandQueue*, QueueFence> g_queue_fences;

// ---- Diagnostics ----

std::atomic<uint64_t> g_enqueued{0};
std::atomic<uint64_t> g_written{0};
std::atomic<uint64_t> g_timeouts{0};
std::atomic<uint64_t> g_stalls{0};

// ---- Trace ----

bool trace_enabled() {
    static const bool on = [] {
        char v[8] = {};
        size_t n = 0;
        return getenv_s(&n, v, sizeof(v), "DEDSEC_CPU_WRITES_TRACE") == 0 && v[0] == '1';
    }();
    return on;
}

void trace(const char* fmt, ...) {
    if (!trace_enabled()) return;
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[rt] cpu_writes: ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

uint32_t timeout_ms() {
    static const uint32_t ms = [] {
        char v[16] = {};
        size_t n = 0;
        if (getenv_s(&n, v, sizeof(v), "DEDSEC_CPU_WRITES_TIMEOUT_MS") == 0 && n > 0) {
            int parsed = std::atoi(v);
            if (parsed > 0) return static_cast<uint32_t>(parsed);
        }
        return 10000u;
    }();
    return ms;
}

// ---- Worker thread ----

void worker_main() {
    HANDLE wait_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!wait_event) {
        std::fprintf(stderr, "[rt] cpu_writes: worker CreateEvent failed (%lu)\n", GetLastError());
        return;
    }

    while (g_running.load(std::memory_order_acquire)) {
        // Dequeue one batch under the lock.
        WriteBatch batch{};
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, [] { return g_count > 0 || !g_running.load(std::memory_order_relaxed); });
            if (!g_running.load(std::memory_order_relaxed) && g_count == 0) break;
            if (g_count == 0) continue;

            batch = g_ring[g_head];
            g_head = (g_head + 1) % kRingCapacity;
            --g_count;
            // Signal any blocked submitter that space is available.
            g_cv.notify_one();
        }

        // Wait for the GPU to reach the fence value, then perform the writes.
        if (batch.fence) {
            HRESULT hr = batch.fence->SetEventOnCompletion(batch.fence_value, wait_event);
            if (SUCCEEDED(hr)) {
                DWORD result = WaitForSingleObject(wait_event, timeout_ms());
                if (result != WAIT_OBJECT_0) {
                    g_timeouts.fetch_add(1, std::memory_order_relaxed);
                    trace("fence wait timed out after %u ms for va=%p value=%llu",
                          timeout_ms(), reinterpret_cast<void*>(batch.va),
                          static_cast<unsigned long long>(batch.fence_value));
                }
            }
        }

        // Perform the CPU-side writes regardless of fence outcome (prevents permanent hangs).
        auto* dst = reinterpret_cast<volatile uint32_t*>(batch.va);
        for (size_t i = 0; i < batch.count; ++i) {
            dst[i] = batch.dwords[i];
        }
        g_written.fetch_add(1, std::memory_order_relaxed);
        trace("wrote %zu dwords at va=%p", batch.count, reinterpret_cast<void*>(batch.va));
    }

    CloseHandle(wait_event);
}

} // namespace

HRESULT initialise() {
    if (g_running.exchange(true, std::memory_order_acq_rel)) return S_OK;

    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_worker = std::thread(worker_main);
    std::fprintf(stderr, "[rt] cpu_writes: initialised (ring capacity %zu, timeout %u ms)\n",
                 kRingCapacity, timeout_ms());
    return S_OK;
}

void shutdown() {
    if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
    g_cv.notify_all();
    if (g_worker.joinable()) g_worker.join();

    // Release all per-queue fences.
    for (auto& [queue, qf] : g_queue_fences) {
        if (qf.fence) qf.fence->Release();
    }
    g_queue_fences.clear();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_head = 0;
    g_tail = 0;
    g_count = 0;
}

HRESULT enqueue(ID3D12CommandQueue* queue, uint64_t fence_value,
                uintptr_t va, const uint32_t* dwords, size_t count) {
    if (!queue || !dwords || count == 0 || count > kMaxBatchDwords) return E_INVALIDARG;
    if (va == 0) return E_INVALIDARG;

    // Find or create a fence for this queue.
    ID3D12Fence* fence = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_queue_fences.find(queue);
        if (it != g_queue_fences.end()) {
            fence = it->second.fence;
        } else {
            // Lazily create a fence via the queue's device.
            ID3D12Device* device = nullptr;
            HRESULT hr = queue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
            if (FAILED(hr) || !device) return E_FAIL;
            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                     reinterpret_cast<void**>(&fence));
            device->Release();
            if (FAILED(hr) || !fence) return hr;
            g_queue_fences[queue] = {fence, 0};
        }
    }

    // Enqueue into the ring buffer, blocking if full.
    {
        std::unique_lock<std::mutex> lock(g_mutex);
        while (g_count >= kRingCapacity && g_running.load(std::memory_order_relaxed)) {
            g_stalls.fetch_add(1, std::memory_order_relaxed);
            g_cv.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (!g_running.load(std::memory_order_relaxed)) return E_ABORT;

        WriteBatch& slot = g_ring[g_tail];
        slot.va = va;
        slot.count = count;
        std::memcpy(slot.dwords, dwords, count * sizeof(uint32_t));
        slot.fence = fence;
        slot.fence_value = fence_value;
        g_tail = (g_tail + 1) % kRingCapacity;
        ++g_count;
    }
    g_enqueued.fetch_add(1, std::memory_order_relaxed);
    g_cv.notify_one();

    trace("enqueued %zu dwords at va=%p fence_value=%llu", count,
          reinterpret_cast<void*>(va), static_cast<unsigned long long>(fence_value));
    return S_OK;
}

void activity(uint64_t out[4]) {
    out[0] = g_enqueued.load(std::memory_order_relaxed);
    out[1] = g_written.load(std::memory_order_relaxed);
    out[2] = g_timeouts.load(std::memory_order_relaxed);
    out[3] = g_stalls.load(std::memory_order_relaxed);
}

} // namespace dedsec::cpu_writes