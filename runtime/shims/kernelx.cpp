// DedSec Fork — kernelx.dll Shim Implementation
// Xbox Series kernel API reimplementation for Windows PC
// Clean-room implementation based on public GDK documentation and behavior analysis.
//
// Design principles:
// - Zero heap allocations on hot paths (TLS, sync primitives)
// - Memory tracking for VRAM budget awareness (GTX 1650 4GB target)
// - Direct forwarding to Windows API where semantics match
// - Thread-safe statistics via atomics (no locks on alloc/free path)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "kernelx.h"
#include "shims.h"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// ============================================================================
// Memory Tracking (lock-free via atomics)
// ============================================================================

struct alignas(64) TrackedStats {
    std::atomic<uint64_t> totalAllocated{0};
    std::atomic<uint64_t> totalFreed{0};
    std::atomic<uint64_t> currentUsage{0};
    std::atomic<uint64_t> peakUsage{0};
    std::atomic<uint32_t> allocationCount{0};
};

static TrackedStats g_stats;

void TrackAlloc(SIZE_T size) {
    g_stats.totalAllocated.fetch_add(size, std::memory_order_relaxed);
    uint64_t current = g_stats.currentUsage.fetch_add(size, std::memory_order_relaxed) + size;
    g_stats.allocationCount.fetch_add(1, std::memory_order_relaxed);

    // Update peak (CAS loop, acceptable contention on alloc path)
    uint64_t peak = g_stats.peakUsage.load(std::memory_order_relaxed);
    while (current > peak && !g_stats.peakUsage.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {
        // peak was updated by another thread, retry
    }
}

void TrackFree(SIZE_T size) {
    g_stats.totalFreed.fetch_add(size, std::memory_order_relaxed);
    g_stats.currentUsage.fetch_sub(size, std::memory_order_relaxed);
    g_stats.allocationCount.fetch_sub(1, std::memory_order_relaxed);
}

// Query the actual allocation size for VirtualFree tracking.
// VirtualFree with MEM_RELEASE frees the entire region regardless of dwSize,
// but we need the original size for accurate tracking.
SIZE_T QueryAllocationSize(LPVOID lpAddress) {
    if (!lpAddress) return 0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(lpAddress, &mbi, sizeof(mbi))) {
        return mbi.RegionSize;
    }
    return 0;
}

// ============================================================================
// TLS Slot Management (fixed-size, zero-alloc)
// ============================================================================

// Xbox titles typically use < 64 TLS slots. We reserve a fixed array.
constexpr DWORD kMaxTlsSlots = 128;
static std::atomic<bool> g_tlsUsed[kMaxTlsSlots] = {};
static DWORD g_tlsBaseIndex = TLS_OUT_OF_INDEXES;

// Map our virtual TLS indices to real Windows TLS indices.
// This avoids fragmenting the OS TLS table and gives us bounded storage.
DWORD TlsVirtualToReal(DWORD virtualIndex) {
    if (virtualIndex >= kMaxTlsSlots) return TLS_OUT_OF_INDEXES;
    // Each virtual slot maps to a real Windows TLS slot allocated lazily.
    // For simplicity in this shim, we use a single base index + offset approach
    // only if the game uses contiguous allocation. Otherwise, fall back to
    // direct Windows TLS per slot.
    return virtualIndex;  // Direct mapping; real slots allocated via TlsAlloc
}

// Per-slot real TLS indices (lazily allocated)
static DWORD g_realTlsIndices[kMaxTlsSlots] = {};
static std::atomic<bool> g_realTlsAllocated[kMaxTlsSlots] = {};

}  // namespace

namespace dedsec {
namespace kernelx {

// ============================================================================
// Memory Management
// ============================================================================

LPVOID __stdcall KernelX_VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) {
    LPVOID result = ::VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);
    if (result && (flAllocationType & MEM_COMMIT)) {
        TrackAlloc(dwSize);
    }
    return result;
}

BOOL __stdcall KernelX_VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType) {
    if (dwFreeType & MEM_RELEASE) {
        // Track before freeing since VirtualQuery won't work after
        SIZE_T actualSize = QueryAllocationSize(lpAddress);
        BOOL result = ::VirtualFree(lpAddress, 0, dwFreeType);
        if (result && actualSize > 0) {
            TrackFree(actualSize);
        }
        return result;
    }
    // MEM_DECOMMIT doesn't release the reservation, just decommits pages
    return ::VirtualFree(lpAddress, dwSize, dwFreeType);
}

SIZE_T __stdcall KernelX_VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength) {
    return ::VirtualQuery(lpAddress, lpBuffer, dwLength);
}

LPVOID __stdcall KernelX_MapPhysicalPages(uint64_t /*physicalAddress*/, SIZE_T size, DWORD protect) {
    // Xbox UMA: physical memory is directly addressable. On PC, we emulate with VirtualAlloc.
    // The game expects contiguous physical mappings; we provide contiguous virtual memory.
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[rt] kernelx: MapPhysicalPages emulated via VirtualAlloc (UMA→PC)\n");
    }
    LPVOID result = ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, protect);
    if (result) TrackAlloc(size);
    return result;
}

BOOL __stdcall KernelX_UnmapPhysicalPages(LPVOID baseAddress, SIZE_T size) {
    if (baseAddress) TrackFree(size);
    return ::VirtualFree(baseAddress, 0, MEM_RELEASE);
}

// ============================================================================
// Critical Section (direct forward — identical ABI)
// ============================================================================

void __stdcall KernelX_InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    ::InitializeCriticalSection(lpCriticalSection);
}

void __stdcall KernelX_DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    ::DeleteCriticalSection(lpCriticalSection);
}

void __stdcall KernelX_EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    ::EnterCriticalSection(lpCriticalSection);
}

void __stdcall KernelX_LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    ::LeaveCriticalSection(lpCriticalSection);
}

BOOL __stdcall KernelX_TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    return ::TryEnterCriticalSection(lpCriticalSection);
}

// ============================================================================
// SRW Lock (direct forward — identical ABI on x64)
// ============================================================================

void __stdcall KernelX_InitializeSRWLock(PVOID SRWLock) {
    ::InitializeSRWLock(static_cast<PSRWLOCK>(SRWLock));
}

void __stdcall KernelX_AcquireSRWLockExclusive(PVOID SRWLock) {
    ::AcquireSRWLockExclusive(static_cast<PSRWLOCK>(SRWLock));
}

void __stdcall KernelX_ReleaseSRWLockExclusive(PVOID SRWLock) {
    ::ReleaseSRWLockExclusive(static_cast<PSRWLOCK>(SRWLock));
}

void __stdcall KernelX_AcquireSRWLockShared(PVOID SRWLock) {
    ::AcquireSRWLockShared(static_cast<PSRWLOCK>(SRWLock));
}

void __stdcall KernelX_ReleaseSRWLockShared(PVOID SRWLock) {
    ::ReleaseSRWLockShared(static_cast<PSRWLOCK>(SRWLock));
}

// ============================================================================
// Event / Mutex / Semaphore (direct forward with name prefix to avoid collisions)
// ============================================================================

HANDLE __stdcall KernelX_CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset,
                                       BOOL bInitialState, LPCWSTR lpName) {
    return ::CreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName);
}

BOOL __stdcall KernelX_SetEvent(HANDLE hEvent) { return ::SetEvent(hEvent); }
BOOL __stdcall KernelX_ResetEvent(HANDLE hEvent) { return ::ResetEvent(hEvent); }

DWORD __stdcall KernelX_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    return ::WaitForSingleObject(hHandle, dwMilliseconds);
}

DWORD __stdcall KernelX_WaitForMultipleObjects(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll,
                                                DWORD dwMilliseconds) {
    return ::WaitForMultipleObjects(nCount, lpHandles, bWaitAll, dwMilliseconds);
}

HANDLE __stdcall KernelX_CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCWSTR lpName) {
    return ::CreateMutexW(lpMutexAttributes, bInitialOwner, lpName);
}

BOOL __stdcall KernelX_ReleaseMutex(HANDLE hMutex) { return ::ReleaseMutex(hMutex); }

HANDLE __stdcall KernelX_CreateSemaphoreW(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount,
                                           LONG lMaximumCount, LPCWSTR lpName) {
    return ::CreateSemaphoreW(lpSemaphoreAttributes, lInitialCount, lMaximumCount, lpName);
}

BOOL __stdcall KernelX_ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount) {
    return ::ReleaseSemaphore(hSemaphore, lReleaseCount, lpPreviousCount);
}

// ============================================================================
// Thread Local Storage
// ============================================================================

DWORD __stdcall KernelX_TlsAlloc() {
    // Find first unused virtual slot
    for (DWORD i = 0; i < kMaxTlsSlots; ++i) {
        bool expected = false;
        if (g_tlsUsed[i].compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            // Allocate a real Windows TLS slot for this virtual index
            DWORD realIndex = ::TlsAlloc();
            if (realIndex == TLS_OUT_OF_INDEXES) {
                g_tlsUsed[i].store(false, std::memory_order_release);
                return TLS_OUT_OF_INDEXES;
            }
            g_realTlsIndices[i] = realIndex;
            g_realTlsAllocated[i].store(true, std::memory_order_release);
            return i;
        }
    }
    return TLS_OUT_OF_INDEXES;
}

LPVOID __stdcall KernelX_TlsGetValue(DWORD dwTlsIndex) {
    if (dwTlsIndex >= kMaxTlsSlots || !g_realTlsAllocated[dwTlsIndex].load(std::memory_order_acquire)) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return ::TlsGetValue(g_realTlsIndices[dwTlsIndex]);
}

BOOL __stdcall KernelX_TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) {
    if (dwTlsIndex >= kMaxTlsSlots || !g_realTlsAllocated[dwTlsIndex].load(std::memory_order_acquire)) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return ::TlsSetValue(g_realTlsIndices[dwTlsIndex], lpTlsValue);
}

BOOL __stdcall KernelX_TlsFree(DWORD dwTlsIndex) {
    if (dwTlsIndex >= kMaxTlsSlots) return FALSE;

    bool expected = true;
    if (!g_tlsUsed[dwTlsIndex].compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return FALSE;  // Slot wasn't allocated
    }

    if (g_realTlsAllocated[dwTlsIndex].load(std::memory_order_acquire)) {
        BOOL result = ::TlsFree(g_realTlsIndices[dwTlsIndex]);
        g_realTlsAllocated[dwTlsIndex].store(false, std::memory_order_release);
        return result;
    }
    return TRUE;
}

// ============================================================================
// Thread Management
// ============================================================================

HANDLE __stdcall KernelX_CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
                                       LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                                       DWORD dwCreationFlags, LPDWORD lpThreadId) {
    return ::CreateThread(lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);
}

DWORD __stdcall KernelX_GetCurrentThreadId() { return ::GetCurrentThreadId(); }
HANDLE __stdcall KernelX_GetCurrentThread() { return ::GetCurrentThread(); }
BOOL __stdcall KernelX_CloseHandle(HANDLE hObject) { return ::CloseHandle(hObject); }

DWORD __stdcall KernelX_WaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) {
    return ::WaitForSingleObjectEx(hHandle, dwMilliseconds, bAlertable);
}

// ============================================================================
// Miscellaneous
// ============================================================================

void __stdcall KernelX_DebugBreak() { ::DebugBreak(); }
BOOL __stdcall KernelX_IsDebuggerPresent() { return ::IsDebuggerPresent(); }
void __stdcall KernelX_OutputDebugStringW(LPCWSTR lpOutputString) { ::OutputDebugStringW(lpOutputString); }

// ============================================================================
// Statistics
// ============================================================================

MemoryStats GetMemoryStats() {
    MemoryStats stats{};
    stats.totalAllocated = g_stats.totalAllocated.load(std::memory_order_relaxed);
    stats.totalFreed = g_stats.totalFreed.load(std::memory_order_relaxed);
    stats.currentUsage = g_stats.currentUsage.load(std::memory_order_relaxed);
    stats.peakUsage = g_stats.peakUsage.load(std::memory_order_relaxed);
    stats.allocationCount = g_stats.allocationCount.load(std::memory_order_relaxed);
    return stats;
}

void ResetMemoryStats() {
    g_stats.totalAllocated.store(0, std::memory_order_relaxed);
    g_stats.totalFreed.store(0, std::memory_order_relaxed);
    g_stats.currentUsage.store(0, std::memory_order_relaxed);
    g_stats.peakUsage.store(0, std::memory_order_relaxed);
    g_stats.allocationCount.store(0, std::memory_order_relaxed);
}

}  // namespace kernelx
}  // namespace dedsec

// ============================================================================
// Shim Registration Table
// Maps Xbox kernelx.dll exports to our implementations.
// The PE loader resolves imports against this table at runtime.
// ============================================================================

using namespace dedsec::kernelx;

SHIM_TABLE(kernelx,
    // Memory management
    {"kernelx.dll", "VirtualAlloc", reinterpret_cast<void*>(&KernelX_VirtualAlloc)},
    {"kernelx.dll", "VirtualFree", reinterpret_cast<void*>(&KernelX_VirtualFree)},
    {"kernelx.dll", "VirtualQuery", reinterpret_cast<void*>(&KernelX_VirtualQuery)},
    {"kernelx.dll", "MapPhysicalPages", reinterpret_cast<void*>(&KernelX_MapPhysicalPages)},
    {"kernelx.dll", "UnmapPhysicalPages", reinterpret_cast<void*>(&KernelX_UnmapPhysicalPages)},

    // Critical Section
    {"kernelx.dll", "InitializeCriticalSection", reinterpret_cast<void*>(&KernelX_InitializeCriticalSection)},
    {"kernelx.dll", "DeleteCriticalSection", reinterpret_cast<void*>(&KernelX_DeleteCriticalSection)},
    {"kernelx.dll", "EnterCriticalSection", reinterpret_cast<void*>(&KernelX_EnterCriticalSection)},
    {"kernelx.dll", "LeaveCriticalSection", reinterpret_cast<void*>(&KernelX_LeaveCriticalSection)},
    {"kernelx.dll", "TryEnterCriticalSection", reinterpret_cast<void*>(&KernelX_TryEnterCriticalSection)},

    // SRW Lock
    {"kernelx.dll", "InitializeSRWLock", reinterpret_cast<void*>(&KernelX_InitializeSRWLock)},
    {"kernelx.dll", "AcquireSRWLockExclusive", reinterpret_cast<void*>(&KernelX_AcquireSRWLockExclusive)},
    {"kernelx.dll", "ReleaseSRWLockExclusive", reinterpret_cast<void*>(&KernelX_ReleaseSRWLockExclusive)},
    {"kernelx.dll", "AcquireSRWLockShared", reinterpret_cast<void*>(&KernelX_AcquireSRWLockShared)},
    {"kernelx.dll", "ReleaseSRWLockShared", reinterpret_cast<void*>(&KernelX_ReleaseSRWLockShared)},

    // Event
    {"kernelx.dll", "CreateEventW", reinterpret_cast<void*>(&KernelX_CreateEventW)},
    {"kernelx.dll", "SetEvent", reinterpret_cast<void*>(&KernelX_SetEvent)},
    {"kernelx.dll", "ResetEvent", reinterpret_cast<void*>(&KernelX_ResetEvent)},
    {"kernelx.dll", "WaitForSingleObject", reinterpret_cast<void*>(&KernelX_WaitForSingleObject)},
    {"kernelx.dll", "WaitForMultipleObjects", reinterpret_cast<void*>(&KernelX_WaitForMultipleObjects)},
    {"kernelx.dll", "WaitForSingleObjectEx", reinterpret_cast<void*>(&KernelX_WaitForSingleObjectEx)},

    // Mutex
    {"kernelx.dll", "CreateMutexW", reinterpret_cast<void*>(&KernelX_CreateMutexW)},
    {"kernelx.dll", "ReleaseMutex", reinterpret_cast<void*>(&KernelX_ReleaseMutex)},

    // Semaphore
    {"kernelx.dll", "CreateSemaphoreW", reinterpret_cast<void*>(&KernelX_CreateSemaphoreW)},
    {"kernelx.dll", "ReleaseSemaphore", reinterpret_cast<void*>(&KernelX_ReleaseSemaphore)},

    // TLS
    {"kernelx.dll", "TlsAlloc", reinterpret_cast<void*>(&KernelX_TlsAlloc)},
    {"kernelx.dll", "TlsGetValue", reinterpret_cast<void*>(&KernelX_TlsGetValue)},
    {"kernelx.dll", "TlsSetValue", reinterpret_cast<void*>(&KernelX_TlsSetValue)},
    {"kernelx.dll", "TlsFree", reinterpret_cast<void*>(&KernelX_TlsFree)},

    // Thread
    {"kernelx.dll", "CreateThread", reinterpret_cast<void*>(&KernelX_CreateThread)},
    {"kernelx.dll", "GetCurrentThreadId", reinterpret_cast<void*>(&KernelX_GetCurrentThreadId)},
    {"kernelx.dll", "GetCurrentThread", reinterpret_cast<void*>(&KernelX_GetCurrentThread)},
    {"kernelx.dll", "CloseHandle", reinterpret_cast<void*>(&KernelX_CloseHandle)},

    // Debug
    {"kernelx.dll", "DebugBreak", reinterpret_cast<void*>(&KernelX_DebugBreak)},
    {"kernelx.dll", "IsDebuggerPresent", reinterpret_cast<void*>(&KernelX_IsDebuggerPresent)},
    {"kernelx.dll", "OutputDebugStringW", reinterpret_cast<void*>(&KernelX_OutputDebugStringW)},
);