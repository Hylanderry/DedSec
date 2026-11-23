// DedSec DXC Runtime — In-process HLSL → DXIL compilation with disk caching
// Loads dxcompiler.dll at runtime from Windows SDK or custom path.
// Thread pool with memory gating (6 GB threshold) to prevent OOM during bulk compilation.
// DXIL cache on disk keyed by FNV-1a hash of source + profile + options.
//
// Adapted from XCL gpu/recomp/dxc.rs logic, rewritten in C++ without attribution.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dedsec {
namespace dxc_runtime {

// Maximum concurrent compile threads — big translated programs take GB each
constexpr int kMaxCompileThreads = 6;

// Initialize the DXC runtime: locate and load dxcompiler.dll + dxil.dll
// Search order: DEDSEC_DXC_DIR env var → exe directory → newest Windows SDK
// Returns true if compiler is available
bool init();

// Check if DXC is loaded and ready
bool is_available();

// Get the folder where dxcompiler.dll was found
std::string get_location();

// Compile HLSL source to DXIL bytecode
// profile: e.g. "vs_6_6", "ps_6_6", "cs_6_6"
// entry: entry point name (usually "main")
// extra_args: additional DXC arguments (e.g. "-O3", "-enable-16bit-types")
// Returns empty vector on failure; error message in out_error if provided
std::vector<uint8_t> compile(const std::string& source,
                              const std::string& entry,
                              const std::string& profile,
                              const std::vector<std::string>& extra_args = {},
                              std::string* out_error = nullptr);

// Compile with disk cache: returns cached DXIL if source hash matches,
// otherwise compiles and stores result in cache_dir/hash.dxil
std::vector<uint8_t> compile_cached(const std::string& source,
                                     const std::string& entry,
                                     const std::string& profile,
                                     const std::string& cache_dir,
                                     const std::vector<std::string>& extra_args = {},
                                     std::string* out_error = nullptr);

// Memory gating: call before starting a compile job.
// Blocks until ≥6 GB of page file is available or timeout (60s).
// Returns a guard object that decrements the working counter on destruction.
struct MemorySlot {
    MemorySlot() = default;
    ~MemorySlot();
    MemorySlot(MemorySlot&&) noexcept;
    MemorySlot& operator=(MemorySlot&&) noexcept;
    MemorySlot(const MemorySlot&) = delete;
    MemorySlot& operator=(const MemorySlot&) = delete;
private:
    bool acquired_ = false;
    friend MemorySlot acquire_memory_slot();
};

MemorySlot acquire_memory_slot();

// Statistics
struct CacheStats {
    uint64_t hits;
    uint64_t misses;
    uint64_t compile_errors;
    uint64_t total_compile_ms;
};

CacheStats get_cache_stats();

} // namespace dxc_runtime
} // namespace dedsec