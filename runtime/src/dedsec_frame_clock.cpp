// DedSec Frame Clock — original implementation for retail game frame pacing.
// See dedsec_frame_clock.h for design notes and environment variables.
#include "dedsec_frame_clock.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace dedsec::frame_clock {
namespace {

// ---- Per-event-type state ----

struct EventType {
    bool scheduled = false;
    uint32_t offset_us = 0;
    HANDLE signal_handle = nullptr; // auto-reset event the waiter blocks on
    const void* ancillary = nullptr;
};

// ---- Shared mutable state (guarded by g_mutex) ----

std::mutex g_mutex;
EventType g_types[kMaxEventTypes];
uint32_t g_interval_us = 0; // 0 = clock disabled
uint64_t g_generation = 0; // bumped on every schedule change so the clock thread detects stale snapshots
HANDLE g_origin_semaphore = nullptr;
std::atomic<bool> g_running{false};
std::thread g_clock_thread;

// ---- Diagnostic counters (lock-free) ----

std::atomic<uint64_t> g_origins_fired{0};
std::atomic<uint64_t> g_custom_fired{0};
std::atomic<uint64_t> g_waits_ok{0};
std::atomic<uint64_t> g_waits_timeout{0};

// ---- Trace helper ----

bool trace_enabled() {
    static const bool on = [] {
        char v[8] = {};
        size_t n = 0;
        return getenv_s(&n, v, sizeof(v), "DEDSEC_FRAME_CLOCK_TRACE") == 0 && v[0] == '1';
    }();
    return on;
}

void trace(const char* fmt, ...) {
    static std::atomic<uint64_t> count{0};
    if (!trace_enabled()) return;
    if (count.fetch_add(1, std::memory_order_relaxed) >= 40) return;
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[rt] frame_clock: ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

// ---- QPC helpers ----

int64_t qpc_freq() {
    static int64_t f = [] {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        return li.QuadPart;
    }();
    return f;
}

int64_t qpc_now() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

int64_t us_to_ticks(uint32_t us) {
    return static_cast<int64_t>(us) * qpc_freq() / 1'000'000;
}

// ---- Clock thread ----

void clock_thread_main() {
    // Use a waitable timer for sub-ms precision sleep without busy-waiting.
    HANDLE timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    if (!timer) {
        std::fprintf(stderr, "[rt] frame_clock: CreateWaitableTimer failed (%lu)\n", GetLastError());
        return;
    }

    while (g_running.load(std::memory_order_acquire)) {
        // Snapshot the schedule under the lock, then release before sleeping.
        uint32_t interval_us = 0;
        uint64_t gen = 0;
        // Stack-allocated due list: at most kMaxEventTypes entries, no heap allocation per tick.
        struct DueEntry {
            uint32_t type;
            HANDLE handle;
        };
        DueEntry due[kMaxEventTypes];
        size_t due_count = 0;
        int64_t earliest_deadline = 0;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            interval_us = g_interval_us;
            gen = g_generation;
            if (interval_us == 0) {
                // Clock disabled; wait for a schedule change.
                // Release lock, sleep briefly, retry.
            } else {
                // Find the earliest deadline among all scheduled types.
                int64_t now = qpc_now();
                int64_t best = INT64_MAX;
                for (size_t i = 0; i < kMaxEventTypes; ++i) {
                    if (!g_types[i].scheduled) continue;
                    // Simple model: next fire time = now rounded up to next interval boundary + offset.
                    int64_t frame_start = now - (now % us_to_ticks(interval_us));
                    int64_t candidate = frame_start + us_to_ticks(g_types[i].offset_us);
                    if (candidate <= now) candidate += us_to_ticks(interval_us);
                    if (candidate < best) best = candidate;
                }
                if (best == INT64_MAX) {
                    // Nothing scheduled; wait for a change.
                } else {
                    earliest_deadline = best;
                    // Collect every type whose deadline matches the earliest (within 1 µs tolerance).
                    for (size_t i = 0; i < kMaxEventTypes; ++i) {
                        if (!g_types[i].scheduled) continue;
                        int64_t frame_start = now - (now % us_to_ticks(interval_us));
                        int64_t candidate = frame_start + us_to_ticks(g_types[i].offset_us);
                        if (candidate <= now) candidate += us_to_ticks(interval_us);
                        if (candidate <= earliest_deadline + us_to_ticks(1)) {
                            due[due_count++] = {static_cast<uint32_t>(i), g_types[i].signal_handle};
                        }
                    }
                }
            }
        }

        if (interval_us == 0 || due_count == 0) {
            // Nothing to fire; sleep 1 ms and re-check.
            Sleep(1);
            continue;
        }

        // Sleep until the deadline using the waitable timer.
        int64_t now = qpc_now();
        if (earliest_deadline > now) {
            int64_t wait_us = (earliest_deadline - now) * 1'000'000 / qpc_freq();
            if (wait_us > 0) {
                LARGE_INTEGER ft;
                ft.QuadPart = -static_cast<LONGLONG>(wait_us * 10); // negative = relative, in 100ns units
                SetWaitableTimer(timer, &ft, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, static_cast<DWORD>(wait_us / 1000 + 2));
            }
        }

        // Verify generation hasn't changed while we slept.
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_generation != gen) continue; // schedule changed; re-evaluate
        }

        // Fire all due events.
        for (size_t i = 0; i < due_count; ++i) {
            if (due[i].type == 0) {
                // Origin: release semaphore + record timestamp.
                ReleaseSemaphore(g_origin_semaphore, 1, nullptr);
                g_origins_fired.fetch_add(1, std::memory_order_relaxed);
            } else {
                SetEvent(due[i].handle);
                g_custom_fired.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    CloseHandle(timer);
}

} // namespace

HRESULT initialise() {
    if (g_running.exchange(true, std::memory_order_acq_rel)) return S_OK; // already running

    g_origin_semaphore = CreateSemaphoreW(nullptr, 0, kOriginSemaphoreMax, nullptr);
    if (!g_origin_semaphore) {
        g_running.store(false, std::memory_order_release);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Create auto-reset events for each non-origin event type.
    for (size_t i = 1; i < kMaxEventTypes; ++i) {
        g_types[i].signal_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_types[i].signal_handle) {
            // Cleanup on partial failure.
            for (size_t j = 1; j < i; ++j) {
                CloseHandle(g_types[j].signal_handle);
                g_types[j].signal_handle = nullptr;
            }
            CloseHandle(g_origin_semaphore);
            g_origin_semaphore = nullptr;
            g_running.store(false, std::memory_order_release);
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    g_clock_thread = std::thread(clock_thread_main);
    std::fprintf(stderr, "[rt] frame_clock: initialised (max %zu types, origin semaphore cap %u)\n",
                 kMaxEventTypes, kOriginSemaphoreMax);
    return S_OK;
}

void shutdown() {
    if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
    if (g_clock_thread.joinable()) g_clock_thread.join();

    for (size_t i = 1; i < kMaxEventTypes; ++i) {
        if (g_types[i].signal_handle) {
            CloseHandle(g_types[i].signal_handle);
            g_types[i].signal_handle = nullptr;
        }
    }
    if (g_origin_semaphore) {
        CloseHandle(g_origin_semaphore);
        g_origin_semaphore = nullptr;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& t : g_types) t = {};
    g_interval_us = 0;
    ++g_generation;
}

HRESULT schedule(uint32_t type, uint32_t offset_us, const void* ancillary, uint32_t flags) {
    trace("ScheduleFrameEventX(type %u, offset %u us, block %p, flags %#x)", type, offset_us, ancillary, flags);

    if (type >= kMaxEventTypes) return E_INVALIDARG;

    const bool unschedule = (flags & 1u) != 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (unschedule) {
        g_types[type].scheduled = false;
    } else {
        g_types[type].scheduled = true;
        g_types[type].offset_us = offset_us;
        g_types[type].ancillary = ancillary;
    }
    ++g_generation;
    return S_OK;
}

HRESULT wait(uint32_t type, uint32_t timeout_ms, const void* /*ancillary*/, uint32_t /*flags*/,
             uint64_t* token_in_out) {
    trace("WaitFrameEventX(type %u, timeout %u ms, token_in %#llx)", type, timeout_ms,
          token_in_out ? static_cast<unsigned long long>(*token_in_out) : 0ULL);

    if (type >= kMaxEventTypes) return E_INVALIDARG;

    HANDLE h = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_types[type].scheduled) return E_INVALIDARG;
        h = (type == 0) ? g_origin_semaphore : g_types[type].signal_handle;
    }

    DWORD result = WaitForSingleObject(h, timeout_ms);
    if (result == WAIT_OBJECT_0) {
        g_waits_ok.fetch_add(1, std::memory_order_relaxed);
        if (token_in_out) ++(*token_in_out);
        return S_OK;
    }
    g_waits_timeout.fetch_add(1, std::memory_order_relaxed);
    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

void set_interval(uint32_t interval_us) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_interval_us = interval_us;
    ++g_generation;
    std::fprintf(stderr, "[rt] frame_clock: interval set to %u us (%.2f ms)\n", interval_us,
                 interval_us / 1000.0);
}

void activity(uint64_t out[4]) {
    out[0] = g_origins_fired.load(std::memory_order_relaxed);
    out[1] = g_custom_fired.load(std::memory_order_relaxed);
    out[2] = g_waits_ok.load(std::memory_order_relaxed);
    out[3] = g_waits_timeout.load(std::memory_order_relaxed);
}

} // namespace dedsec::frame_clock