// DedSec Fork — DX12 Native Pipeline
// Persistent Shader Cache — avoids HLSL→DXIL recompilation on every game load
//
// Architecture:
// - Hash RDNA2 bytecode + translation options → cache key
// - Store compiled DXIL blob on disk (per-GPU-vendor directory)
// - On subsequent loads, load from cache if hash matches
// - Falls back to runtime compilation if cache miss or version mismatch
//
// Cache layout:
//   %APPDATA%/DedSec/ShaderCache/{GPU_VENDOR}_{DRIVER_VERSION}/
//     {hash}.dxil       — compiled DXIL blob
//     {hash}.meta       — metadata (RDNA2 hash, shader stage, creation time)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <d3d12.h>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dedsec {

struct ShaderCacheKey {
    uint64_t rdna2_hash;      // FNV-1a hash of RDNA2 bytecode
    uint32_t options;          // translation flags (wave32, fp16, etc.)
    uint8_t  stage;            // ShaderStage enum
    uint8_t  reserved[3];
};

struct ShaderCacheEntry {
    std::vector<uint8_t> dxil_blob;
    uint64_t rdna2_hash;
    uint32_t options;
    uint8_t  stage;
    uint64_t creation_time;    // Unix timestamp
    uint32_t driver_version;   // GPU driver version at compile time
};

// Thread-safe persistent shader cache
class ShaderCache {
public:
    // Initialize cache with GPU-specific directory
    // gpu_vendor: "nvidia", "amd", "intel"
    // driver_version: from DXGI adapter description
    static bool initialize(const std::string& gpu_vendor, uint32_t driver_version);

    // Shutdown and flush any pending entries
    static void shutdown();

    // Lookup compiled DXIL for a given RDNA2 shader
    // Returns true if found, fills out_entry with cached DXIL
    // Returns false on cache miss — caller must compile and call store()
    static bool lookup(const ShaderCacheKey& key, ShaderCacheEntry& out_entry);

    // Store a newly compiled DXIL blob in cache
    // Writes to disk asynchronously
    static void store(const ShaderCacheKey& key, const std::vector<uint8_t>& dxil_blob, uint8_t stage, uint32_t options);

    // Pre-load cache entries for a batch of shaders (e.g., at level load)
    static void warmup(const std::vector<ShaderCacheKey>& keys);

    // Get cache statistics
    static uint64_t get_hits();
    static uint64_t get_misses();
    static uint64_t get_disk_entries();
    static uint64_t get_memory_bytes();

    // Clear entire cache (for debugging or driver update)
    static void clear();

private:
    static std::filesystem::path cache_path(const ShaderCacheKey& key);
    static std::string key_to_filename(const ShaderCacheKey& key);
    static bool read_from_disk(const std::filesystem::path& path, ShaderCacheEntry& entry);
    static bool write_to_disk(const std::filesystem::path& path, const ShaderCacheEntry& entry);

    static std::mutex s_mutex;
    static std::unordered_map<uint64_t, ShaderCacheEntry> s_memory_cache;
    static std::filesystem::path s_cache_dir;
    static uint64_t s_hits;
    static uint64_t s_misses;
    static uint64_t s_disk_entries;
    static bool s_initialized;
};

} // namespace dedsec