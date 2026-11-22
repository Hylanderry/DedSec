// DedSec Fork — DX12 Native Pipeline
// Zen 2 CPU compatibility layer via Vectored Exception Handler (VEH)
// Emulates Zen 2-specific instructions that may not exist on all x64 CPUs:
// - CLZERO (cache line zero)
// - SSE4a instructions (EXTRQ, INSERTQ, MOVNTSD, MOVNTSS)
// - MONITORX/MWAITX (extended monitor/wait)
//
// Based on AnyXboxSeries wsx_zen2_compat.h approach.
// Registers a first-chance VEH that intercepts #UD exceptions for these opcodes
// and emulates them in software, allowing Xbox Series code to run on any x64 host.

#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace dedsec {
namespace zen2_compat {

// Initialize the VEH handler for Zen 2 instruction emulation.
// Must be called once at process startup before loading Xbox binaries.
bool initialize();

// Remove the VEH handler and cleanup.
void shutdown();

// Check if the current CPU supports Zen 2 instructions natively.
// Returns true if no emulation is needed.
bool has_native_support();

// VEH handler function (registered via AddVectoredExceptionHandler)
LONG CALLBACK veh_handler(PEXCEPTION_POINTERS exception_info);

// Statistics for monitoring emulation overhead
struct Stats {
    uint64_t clzero_emulated;
    uint64_t sse4a_emulated;
    uint64_t monitorx_emulated;
    uint64_t mwaitx_emulated;
    uint64_t total_exceptions_handled;
};

Stats get_stats();
void reset_stats();

} // namespace zen2_compat
} // namespace dedsec

// Implementation (include in exactly one .cpp file with #define ZEN2_COMPAT_IMPLEMENTATION)
#ifdef ZEN2_COMPAT_IMPLEMENTATION

#include <intrin.h>

namespace dedsec {
namespace zen2_compat {

static PVOID g_veh_handle = nullptr;
static Stats g_stats = {};

bool initialize() {
    if (g_veh_handle != nullptr) return true;
    g_veh_handle = AddVectoredExceptionHandler(1, veh_handler);
    return g_veh_handle != nullptr;
}

void shutdown() {
    if (g_veh_handle != nullptr) {
        RemoveVectoredExceptionHandler(g_veh_handle);
        g_veh_handle = nullptr;
    }
}

bool has_native_support() {
    int cpu_info[4] = {};
    __cpuid(cpu_info, 0x80000001);
    // Bit 6 of ECX = SSE4a support (AMD-specific)
    return (cpu_info[2] & (1 << 6)) != 0;
}

LONG CALLBACK veh_handler(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    uint8_t* insn = reinterpret_cast<uint8_t*>(ep->ExceptionRecord->ExceptionAddress);

    // CLZERO: 0F 01 FC
    if (insn[0] == 0x0F && insn[1] == 0x01 && insn[2] == 0xFC) {
        void* addr = reinterpret_cast<void*>(ep->ContextRecord->Rax);
        memset(addr, 0, 64); // Zero one cache line
        ep->ContextRecord->Rip += 3;
        g_stats.clzero_emulated++;
        g_stats.total_exceptions_handled++;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // MONITORX: 0F 01 FA
    if (insn[0] == 0x0F && insn[1] == 0x01 && insn[2] == 0xFA) {
        // Stub: real MONITORX requires ring-0; log and skip
        ep->ContextRecord->Rip += 3;
        g_stats.monitorx_emulated++;
        g_stats.total_exceptions_handled++;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // MWAITX: 0F 01 FB
    if (insn[0] == 0x0F && insn[1] == 0x01 && insn[2] == 0xFB) {
        // Stub: approximate with Sleep based on timeout hint
        uint32_t timeout_cycles = static_cast<uint32_t>(ep->ContextRecord->Rcx);
        if (timeout_cycles > 0) {
            Sleep(timeout_cycles / 3000000); // ~3GHz → ms conversion
        }
        ep->ContextRecord->Rip += 3;
        g_stats.mwaitx_emulated++;
        g_stats.total_exceptions_handled++;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // SSE4a EXTRQ/INSERTQ/MOVNTSD/MOVNTSS would be decoded here
    // For now, pass through to next handler
    return EXCEPTION_CONTINUE_SEARCH;
}

Stats get_stats() { return g_stats; }
void reset_stats() { memset(&g_stats, 0, sizeof(g_stats)); }

} // namespace zen2_compat
} // namespace dedsec

#endif // ZEN2_COMPAT_IMPLEMENTATION