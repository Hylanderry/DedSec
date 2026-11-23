// DedSec Fork — kernelx.dll Shim Unit Tests
// Tests memory management, TLS, sync primitives, and thread creation.
// No game data required; runs standalone on Windows.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "../shims/kernelx.h"

#include <cstdio>
#include <cstring>
#include <cassert>

using namespace dedsec::kernelx;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct TestReg_##name { \
        TestReg_##name() { \
            std::printf("  %-50s", #name); \
            try { \
                test_##name(); \
                std::printf("PASS\n"); \
                g_testsPassed++; \
            } catch (...) { \
                std::printf("FAIL (exception)\n"); \
                g_testsFailed++; \
            } \
        } \
    } g_testReg_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    if (!(expr)) { \
        std::fprintf(stderr, "\n    ASSERT FAILED: %s (line %d)\n", #expr, __LINE__); \
        throw 1; \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::fprintf(stderr, "\n    ASSERT FAILED: %s == %s (line %d)\n", #a, #b, __LINE__); \
        throw 1; \
    }

// ============================================================================
// Memory Management Tests
// ============================================================================

TEST(VirtualAlloc_Basic) {
    LPVOID ptr = KernelX_VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ASSERT_TRUE(ptr != nullptr);

    // Write to verify the mapping works
    std::memset(ptr, 0xAA, 4096);
    uint8_t* bytes = static_cast<uint8_t*>(ptr);
    ASSERT_EQ(bytes[0], 0xAA);
    ASSERT_EQ(bytes[4095], 0xAA);

    BOOL freed = KernelX_VirtualFree(ptr, 0, MEM_RELEASE);
    ASSERT_TRUE(freed);
}

TEST(VirtualAlloc_Tracking) {
    ResetMemoryStats();

    LPVOID ptr = KernelX_VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ASSERT_TRUE(ptr != nullptr);

    MemoryStats stats = GetMemoryStats();
    ASSERT_TRUE(stats.totalAllocated >= 8192);
    ASSERT_TRUE(stats.currentUsage >= 8192);
    ASSERT_TRUE(stats.allocationCount >= 1);

    KernelX_VirtualFree(ptr, 0, MEM_RELEASE);

    stats = GetMemoryStats();
    ASSERT_TRUE(stats.totalFreed >= 8192);
    ASSERT_EQ(stats.currentUsage, 0u);

    ResetMemoryStats();
}

TEST(VirtualQuery_Basic) {
    LPVOID ptr = KernelX_VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ASSERT_TRUE(ptr != nullptr);

    MEMORY_BASIC_INFORMATION mbi{};
    SIZE_T result = KernelX_VirtualQuery(ptr, &mbi, sizeof(mbi));
    ASSERT_TRUE(result > 0);
    ASSERT_EQ(mbi.State, MEM_COMMIT);
    ASSERT_EQ(mbi.Protect, PAGE_READWRITE);

    KernelX_VirtualFree(ptr, 0, MEM_RELEASE);
}

TEST(MapPhysicalPages_Emulation) {
    LPVOID ptr = KernelX_MapPhysicalPages(0x1000, 4096, PAGE_READWRITE);
    ASSERT_TRUE(ptr != nullptr);

    // Verify it's usable memory
    std::memset(ptr, 0xBB, 4096);
    uint8_t* bytes = static_cast<uint8_t*>(ptr);
    ASSERT_EQ(bytes[0], 0xBB);

    BOOL unmapped = KernelX_UnmapPhysicalPages(ptr, 4096);
    ASSERT_TRUE(unmapped);
}

// ============================================================================
// Critical Section Tests
// ============================================================================

TEST(CriticalSection_Basic) {
    CRITICAL_SECTION cs;
    KernelX_InitializeCriticalSection(&cs);

    KernelX_EnterCriticalSection(&cs);
    BOOL tried = KernelX_TryEnterCriticalSection(&cs);
    ASSERT_TRUE(tried); // Same thread can re-enter
    KernelX_LeaveCriticalSection(&cs);

    KernelX_LeaveCriticalSection(&cs);
    KernelX_DeleteCriticalSection(&cs);
}

// ============================================================================
// SRW Lock Tests
// ============================================================================

TEST(SRWLock_Exclusive) {
    SRWLOCK lock = SRWLOCK_INIT;
    KernelX_InitializeSRWLock(&lock);

    KernelX_AcquireSRWLockExclusive(&lock);
    KernelX_ReleaseSRWLockExclusive(&lock);
}

TEST(SRWLock_Shared) {
    SRWLOCK lock = SRWLOCK_INIT;
    KernelX_InitializeSRWLock(&lock);

    KernelX_AcquireSRWLockShared(&lock);
    KernelX_ReleaseSRWLockShared(&lock);
}

// ============================================================================
// Event Tests
// ============================================================================

TEST(Event_CreateSetReset) {
    HANDLE event = KernelX_CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT_TRUE(event != nullptr);

    // Initially non-signaled
    DWORD waitResult = KernelX_WaitForSingleObject(event, 0);
    ASSERT_EQ(waitResult, WAIT_TIMEOUT);

    // Signal it
    BOOL set = KernelX_SetEvent(event);
    ASSERT_TRUE(set);

    // Now signaled
    waitResult = KernelX_WaitForSingleObject(event, 0);
    ASSERT_EQ(waitResult, WAIT_OBJECT_0);

    // Reset
    BOOL reset = KernelX_ResetEvent(event);
    ASSERT_TRUE(reset);

    waitResult = KernelX_WaitForSingleObject(event, 0);
    ASSERT_EQ(waitResult, WAIT_TIMEOUT);

    KernelX_CloseHandle(event);
}

// ============================================================================
// Mutex Tests
// ============================================================================

TEST(Mutex_CreateRelease) {
    HANDLE mutex = KernelX_CreateMutexW(nullptr, FALSE, nullptr);
    ASSERT_TRUE(mutex != nullptr);

    DWORD waitResult = KernelX_WaitForSingleObject(mutex, 0);
    ASSERT_EQ(waitResult, WAIT_OBJECT_0);

    BOOL released = KernelX_ReleaseMutex(mutex);
    ASSERT_TRUE(released);

    KernelX_CloseHandle(mutex);
}

// ============================================================================
// Semaphore Tests
// ============================================================================

TEST(Semaphore_CreateRelease) {
    HANDLE sem = KernelX_CreateSemaphoreW(nullptr, 2, 10, nullptr);
    ASSERT_TRUE(sem != nullptr);

    // Should be able to acquire twice (initial count = 2)
    DWORD r1 = KernelX_WaitForSingleObject(sem, 0);
    ASSERT_EQ(r1, WAIT_OBJECT_0);

    DWORD r2 = KernelX_WaitForSingleObject(sem, 0);
    ASSERT_EQ(r2, WAIT_OBJECT_0);

    // Third should timeout
    DWORD r3 = KernelX_WaitForSingleObject(sem, 0);
    ASSERT_EQ(r3, WAIT_TIMEOUT);

    // Release one
    LONG prev = 0;
    BOOL rel = KernelX_ReleaseSemaphore(sem, 1, &prev);
    ASSERT_TRUE(rel);
    ASSERT_EQ(prev, 0L);

    KernelX_CloseHandle(sem);
}

// ============================================================================
// TLS Tests
// ============================================================================

TEST(TLS_AllocSetGetFree) {
    DWORD slot = KernelX_TlsAlloc();
    ASSERT_TRUE(slot != TLS_OUT_OF_INDEXES);

    // Initial value should be null
    LPVOID val = KernelX_TlsGetValue(slot);
    ASSERT_TRUE(val == nullptr);

    // Set a value
    int testData = 42;
    BOOL set = KernelX_TlsSetValue(slot, &testData);
    ASSERT_TRUE(set);

    // Get it back
    LPVOID retrieved = KernelX_TlsGetValue(slot);
    ASSERT_TRUE(retrieved == &testData);

    // Free the slot
    BOOL freed = KernelX_TlsFree(slot);
    ASSERT_TRUE(freed);
}

TEST(TLS_MultipleSlots) {
    DWORD slots[8];
    for (int i = 0; i < 8; ++i) {
        slots[i] = KernelX_TlsAlloc();
        ASSERT_TRUE(slots[i] != TLS_OUT_OF_INDEXES);
    }

    // Each slot should be independent
    int values[8];
    for (int i = 0; i < 8; ++i) {
        values[i] = i * 100;
        KernelX_TlsSetValue(slots[i], &values[i]);
    }

    for (int i = 0; i < 8; ++i) {
        int* p = static_cast<int*>(KernelX_TlsGetValue(slots[i]));
        ASSERT_TRUE(p == &values[i]);
        ASSERT_EQ(*p, i * 100);
    }

    for (int i = 0; i < 8; ++i) {
        KernelX_TlsFree(slots[i]);
    }
}

TEST(TLS_InvalidSlot) {
    LPVOID val = KernelX_TlsGetValue(99999);
    ASSERT_TRUE(val == nullptr);

    BOOL set = KernelX_TlsSetValue(99999, nullptr);
    ASSERT_TRUE(!set);

    BOOL freed = KernelX_TlsFree(99999);
    ASSERT_TRUE(!freed);
}

// ============================================================================
// Thread Tests
// ============================================================================

static DWORD WINAPI ThreadProc(LPVOID param) {
    int* counter = static_cast<int*>(param);
    *counter = 12345;
    return 0;
}

TEST(Thread_CreateAndWait) {
    int counter = 0;
    DWORD threadId = 0;

    HANDLE thread = KernelX_CreateThread(nullptr, 0, ThreadProc, &counter, 0, &threadId);
    ASSERT_TRUE(thread != nullptr);
    ASSERT_TRUE(threadId != 0);

    DWORD waitResult = KernelX_WaitForSingleObject(thread, 5000);
    ASSERT_EQ(waitResult, WAIT_OBJECT_0);
    ASSERT_EQ(counter, 12345);

    KernelX_CloseHandle(thread);
}

TEST(Thread_GetCurrentId) {
    DWORD id = KernelX_GetCurrentThreadId();
    ASSERT_TRUE(id != 0);

    HANDLE self = KernelX_GetCurrentThread();
    ASSERT_TRUE(self != nullptr);
}

// ============================================================================
// Miscellaneous Tests
// ============================================================================

TEST(Debug_IsDebuggerPresent) {
    // Just verify it doesn't crash; result depends on environment
    (void)KernelX_IsDebuggerPresent();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("=== kernelx.dll shim tests ===\n");

    // Tests auto-register via static constructors above

    std::printf("\n--- Results ---\n");
    std::printf("Passed: %d\n", g_testsPassed);
    std::printf("Failed: %d\n", g_testsFailed);

    return g_testsFailed > 0 ? 1 : 0;
}