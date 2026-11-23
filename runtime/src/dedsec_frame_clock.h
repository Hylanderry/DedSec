// DedSec Frame Clock: schedules and fires per-frame timing events that the retail title uses to
// pace its render loop. The console SDK exposes ScheduleFrameEventX / WaitFrameEventX through the
// D3D12.X device vtable; without a working implementation the title hangs after two frames because
// it waits on an event type that never fires.
//
// Design notes (DedSec original implementation):
// - Multiple event types sharing the same deadline fire together in one clock tick. This is the
//   fix that unblocked the retail game: it schedules origin (type 0) and type 2 at offset 0 and
//   waits on type 2; picking only the earliest type meant type 2 never came.
// - The clock thread uses a single high-resolution timer and wakes once per deadline, signaling
//   every due event type before sleeping again. No busy-wait, no per-type threads.
// - VRAM-conscious: no per-frame allocations. Event state lives in a fixed-size array indexed by
//   type id; the clock thread's due-list is a stack-allocated small vector.
// - Thread-safe: all public entry points acquire the same mutex; the clock thread holds it only
//   to snapshot the schedule, then releases before sleeping/signaling.
//
// Environment variables:
//   DEDSEC_FRAME_CLOCK_TRACE=1  log the first 40 Schedule/Wait calls for debugging.
//   DEDSEC_FRAME_INTERVAL_US=N  override the frame interval in microseconds (default: from title).

#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace dedsec::frame_clock {

// Maximum number of distinct event types the clock tracks. The console SDK defines types 0..7;
// we allocate 16 to absorb any extension without reallocating.
constexpr size_t kMaxEventTypes = 16;

// Maximum number of pending origins the semaphore can hold. Matches the console's internal limit;
// excess origins are silently dropped (the title cannot consume them faster than the frame rate).
constexpr uint32_t kOriginSemaphoreMax = 8;

// Initialise the frame clock subsystem. Safe to call multiple times; only the first call creates
// the clock thread. Returns S_OK on success.
HRESULT initialise();

// Tear down the clock thread and release all resources. Idempotent.
void shutdown();

// ScheduleFrameEventX(type, offset_us, ancillary_block, flags).
// type: event kind index (0 = origin, 1..kMaxEventTypes-1 = custom).
// offset_us: microseconds into the frame at which this event fires.
// ancillary_block: opaque pointer the title associates with this schedule (stored but not read).
// flags: bit 0 = unschedule (cancel a previously scheduled event of this type).
HRESULT schedule(uint32_t type, uint32_t offset_us, const void* ancillary, uint32_t flags);

// WaitFrameEventX(type, timeout_ms, ancillary_block, flags, token_in_out).
// Blocks until the next occurrence of `type` or timeout_ms elapses.
// token_in_out: if non-null, *token_in_out is updated with a monotonic counter for this wait.
HRESULT wait(uint32_t type, uint32_t timeout_ms, const void* ancillary, uint32_t flags,
             uint64_t* token_in_out);

// Set the frame interval in microseconds. Called once during device creation from the title's
// presentation parameters. A zero value disables the clock (all waits return immediately).
void set_interval(uint32_t interval_us);

// Diagnostic counters: [origins_fired, custom_events_fired, waits_satisfied, waits_timed_out].
// Read atomically; safe to sample from any thread.
void activity(uint64_t out[4]);

}  // namespace dedsec::frame_clock