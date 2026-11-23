// DedSec Fork — kernelx.dll Shim Header
// Xbox Series kernel API reimplementation for Windows PC
// Clean-room implementation based on public GDK documentation and behavior analysis.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace dedsec {
namespace kernelx {

// ============================================================================
// Memory Management
// ============================================================================

// VirtualAlloc wrapper with tracking for VRAM budget awareness
LPVOID __stdcall KernelX_VirtualAlloc(
    LPVOID lpAddress,
    SIZE_T dwSize,
    DWORD flAllocationType,
    DWORD flProtect
);

BOOL __stdcall KernelX_VirtualFree(
    LPVOID lpAddress,
    SIZE_T dwSize,
    DWORD dwFreeType
);

SIZE_T __stdcall KernelX_VirtualQuery(
    LPCVOID lpAddress,
    PMEMORY_BASIC_INFORMATION lpBuffer,
    SIZE_T dwLength
);

// Physical memory mapping (Xbox UMA emulation)
LPVOID __stdcall KernelX_MapPhysicalPages(
    uint64_t physicalAddress,
    SIZE_T size,
    DWORD protect
);

BOOL __stdcall KernelX_UnmapPhysicalPages(
    LPVOID baseAddress,
    SIZE_T size
);

// ============================================================================
// Synchronization Primitives
// ============================================================================

// Critical Section
void __stdcall KernelX_InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void __stdcall KernelX_DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void __stdcall KernelX_EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void __stdcall KernelX_LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
BOOL __stdcall KernelX_TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);

// SRW Lock (Slim Reader/Writer)
void __stdcall KernelX_InitializeSRWLock(PVOID SRWLock);
void __stdcall KernelX_AcquireSRWLockExclusive(PVOID SRWLock);
void __stdcall KernelX_ReleaseSRWLockExclusive(PVOID SRWLock);
void __stdcall KernelX_AcquireSRWLockShared(PVOID SRWLock);
void __stdcall KernelX_ReleaseSRWLockShared(PVOID SRWLock);

// Event
HANDLE __stdcall KernelX_CreateEventW(
    LPSECURITY_ATTRIBUTES lpEventAttributes,
    BOOL bManualReset,
    BOOL bInitialState,
    LPCWSTR lpName
);

BOOL __stdcall KernelX_SetEvent(HANDLE hEvent);
BOOL __stdcall KernelX_ResetEvent(HANDLE hEvent);
DWORD __stdcall KernelX_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD __stdcall KernelX_WaitForMultipleObjects(
    DWORD nCount,
    const HANDLE* lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds
);

// Mutex
HANDLE __stdcall KernelX_CreateMutexW(
    LPSECURITY_ATTRIBUTES lpMutexAttributes,
    BOOL bInitialOwner,
    LPCWSTR lpName
);

BOOL __stdcall KernelX_ReleaseMutex(HANDLE hMutex);

// Semaphore
HANDLE __stdcall KernelX_CreateSemaphoreW(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    LPCWSTR lpName
);

BOOL __stdcall KernelX_ReleaseSemaphore(
    HANDLE hSemaphore,
    LONG lReleaseCount,
    LPLONG lpPreviousCount
);

// ============================================================================
// Thread Local Storage
// ============================================================================

DWORD __stdcall KernelX_TlsAlloc();
LPVOID __stdcall KernelX_TlsGetValue(DWORD dwTlsIndex);
BOOL __stdcall KernelX_TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue);
BOOL __stdcall KernelX_TlsFree(DWORD dwTlsIndex);

// ============================================================================
// Thread Management
// ============================================================================

HANDLE __stdcall KernelX_CreateThread(
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    SIZE_T dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress,
    LPVOID lpParameter,
    DWORD dwCreationFlags,
    LPDWORD lpThreadId
);

DWORD __stdcall KernelX_GetCurrentThreadId();
HANDLE __stdcall KernelX_GetCurrentThread();
BOOL __stdcall KernelX_CloseHandle(HANDLE hObject);
DWORD __stdcall KernelX_WaitForSingleObjectEx(
    HANDLE hHandle,
    DWORD dwMilliseconds,
    BOOL bAlertable
);

// ============================================================================
// Miscellaneous
// ============================================================================

void __stdcall KernelX_DebugBreak();
BOOL __stdcall KernelX_IsDebuggerPresent();
void __stdcall KernelX_OutputDebugStringW(LPCWSTR lpOutputString);

// Memory tracking statistics (for VRAM budget monitoring)
struct MemoryStats {
    uint64_t totalAllocated;
    uint64_t totalFreed;
    uint64_t currentUsage;
    uint64_t peakUsage;
    uint32_t allocationCount;
};

MemoryStats GetMemoryStats();
void ResetMemoryStats();

} // namespace kernelx
} // namespace dedsec