// DedSec CPU Writes Bottom-of-Pipe: executes GPU-invisible memory writes on the CPU after the
// GPU has finished the work preceding them. Console titles mark progress with Write32/64BitValue
// BottomOfPipeX into plain VirtualAlloc memory and wait on those values; the PC GPU cannot write
// there, so without this shim the title hangs after its second frame.
//
// Design notes (DedSec original implementation):
// - One fence per command queue. After a submission whose lists carry bottom-of-pipe writes, the
//   queue signals its fence and a single worker thread performs the writes in submission order
//   once the GPU reaches that point. This is what "bottom of pipe" means on PC.
// - VRAM-conscious: no per-submission heap allocations. Write payloads are stored in a fixed-capacity
//   ring buffer; overflow blocks the submitter until the worker drains entries.
// - Thread-safe: submissions enqueue under a mutex + condition variable; the worker dequeues
//   lock-free between fence waits.
// - Timeout safety: if the device is removed or a fence never signals, the worker writes anyway
//   after a configurable timeout to prevent permanent hangs.
//
// Environment variables:
// DEDSEC_CPU_WRITES_TRACE=1 log every enqueued write batch for debugging.
// DEDSEC_CPU_WRITES_TIMEOUT_MS=N override the fence wait timeout (default: 10000 ms).

#pragma once

#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstddef>

namespace dedsec::cpu_writes {

// Maximum number of dwords in a single write batch. Covers the largest observed console payload
// (typically 1-4 dwords for fence/status values). Larger payloads are split across batches.
constexpr size_t kMaxBatchDwords = 64;

// Ring buffer capacity: maximum number of pending write batches before the submitter blocks.
// Sized for ~2 frames of 60 Hz work at typical batch rates; increase if profiling shows stalls.
constexpr size_t kRingCapacity = 256;

// Initialise the CPU writes subsystem. Creates the worker thread and internal fence pool.
// Safe to call multiple times; only the first call creates resources. Returns S_OK on success.
HRESULT initialise();

// Tear down the worker thread and release all fences. Idempotent. Blocks until the worker exits.
void shutdown();

// Enqueue a bottom-of-pipe write batch associated with a specific command queue.
// queue: the ID3D12CommandQueue that will signal completion of the preceding GPU work.
// va: virtual address of the first dword to write (must be in process-accessible memory).
// dwords: pointer to the values to write, count elements.
// count: number of dwords to write (must be <= kMaxBatchDwords).
//
// The caller must ensure the queue has been submitted and will signal a fence value >= the one
// passed here. Typically called right after ExecuteCommandLists on the same queue.
//
// Returns S_OK if enqueued, E_INVALIDARG for bad parameters, E_OUTOFMEMORY if the ring is full
// and the worker cannot drain within the timeout.
HRESULT enqueue(ID3D12CommandQueue* queue, uint64_t fence_value,
                uintptr_t va, const uint32_t* dwords, size_t count);

// Diagnostic counters: [batches_enqueued, batches_written, fence_timeouts, ring_full_stalls].
void activity(uint64_t out[4]);

} // namespace dedsec::cpu_writes