// DedSec Fork — DX12 Native Pipeline
// Persistent Shader Cache — Implementation
// Built from scratch for the DedSec DX12 native pipeline.
//
// Stores compiled DXIL blobs on disk keyed by RDNA2 bytecode hash.
// Memory cache (LRU-style via unordered_map) avoids repeated disk I/O.
// Per-GPU-vendor directories prevent cross-vendor DXIL incompatibility.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_SHADER_CACHE_IMPLEMENTATION
#include "dedsec_shader_cache.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace dedsec {

// ─── Static state ─────────────────────────────────────────────────────────────
std::mutex ShaderCache::s_mutex;
std::unordered_map<uint64_t, ShaderCacheEntry> ShaderCache::s_memory_cache;
std::filesystem::path ShaderCache::s_cache_dir;
uint64_t ShaderCache::s_hits = 0;
uint64_t ShaderCache::s_misses = 0;
uint64_t ShaderCache::s_disk_entries = 0;
bool ShaderCache::s_initialized = false;

// ─── FNV-1a helper for combining key fields ───────────────────────────────────
static uint64_t fnv1a_combine(const ShaderCacheKey& key) {
    // Simple combination: rdna2_hash is already a good hash, mix in options and stage
    uint64_t h = key.rdna2_hash;
    h ^= static_cast<uint64_t>(key.options) * 0x100000001B3ULL;
    h ^= static_cast<uint64_t>(key.stage) * 0x0100000001B3ULL;
    return h;
}

// ─── Key to filename ──────────────────────────────────────────────────────────
std::string ShaderCache::key_to_filename(const ShaderCacheKey& key) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << key.rdna2_hash
        << "_" << std::setw(8) << key.options
        << "_" << std::setw(2) << static_cast<int>(key.stage);
    return oss.str();
}

std::filesystem::path ShaderCache::cache_path(const ShaderCacheKey& key) {
    return s_cache_dir / (key_to_filename(key) + ".dxil");
}

// ─── Disk I/O ─────────────────────────────────────────────────────────────────
bool ShaderCache::read_from_disk(const std::filesystem::path& path, ShaderCacheEntry& entry) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // Header: rdna2_hash(8) + options(4) + stage(1) + creation_time(8) + driver_version(4) + blob_size(4) = 29 bytes
    uint8_t header[29];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file.good()) return false;

    std::memcpy(&entry.rdna2_hash, header + 0, 8);
    std::memcpy(&entry.options, header + 8, 4);
    std::memcpy(&entry.stage, header + 12, 1);
    std::memcpy(&entry.creation_time, header + 13, 8);
    std::memcpy(&entry.driver_version, header + 21, 4);

    uint32_t blob_size = 0;
    std::memcpy(&blob_size, header + 25, 4);

    if (blob_size == 0 || blob_size > 10 * 1024 * 1024) return false; // Sanity: max 10MB per shader

    entry.dxil_blob.resize(blob_size);
    file.read(reinterpret_cast<char*>(entry.dxil_blob.data()), blob_size);
    return file.good();
}

bool ShaderCache::write_to_disk(const std::filesystem::path& path, const ShaderCacheEntry& entry) {
    // Ensure parent directory exists
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    // Write header
    uint8_t header[29] = {};
    std::memcpy(header + 0, &entry.rdna2_hash, 8);
    std::memcpy(header + 8, &entry.options, 4);
    std::memcpy(header + 12, &entry.stage, 1);
    std::memcpy(header + 13, &entry.creation_time, 8);
    std::memcpy(header + 21, &entry.driver_version, 4);
    uint32_t blob_size = static_cast<uint32_t>(entry.dxil_blob.size());
    std::memcpy(header + 25, &blob_size, 4);

    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(reinterpret_cast<const char*>(entry.dxil_blob.data()), blob_size);
    return file.good();
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool ShaderCache::initialize(const std::string& gpu_vendor, uint32_t driver_version) {
    std::lock_guard lock(s_mutex);
    if (s_initialized) return true;

#ifdef _WIN32
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::fprintf(stderr, "[DedSec Cache] Failed to get APPDATA path\n");
        return false;
    }
    s_cache_dir = std::filesystem::path(appdata) / "DedSec" / "ShaderCache";
#else
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    s_cache_dir = std::filesystem::path(home) / ".dedsec" / "shader_cache";
#endif

    // Append GPU-specific subdirectory
    std::ostringstream subdir;
    subdir << gpu_vendor << "_" << driver_version;
    s_cache_dir /= subdir.str();

    std::error_code ec;
    std::filesystem::create_directories(s_cache_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[DedSec Cache] Failed to create cache dir: %s\n", ec.message().c_str());
        return false;
    }

    // Count existing entries
    for (const auto& entry : std::filesystem::directory_iterator(s_cache_dir, ec)) {
        if (entry.path().extension() == ".dxil") s_disk_entries++;
    }

    s_initialized = true;
    std::printf("[DedSec Cache] Initialized: %s (%llu entries on disk)\n",
                s_cache_dir.string().c_str(), static_cast<unsigned long long>(s_disk_entries));
    return true;
}

void ShaderCache::shutdown() {
    std::lock_guard lock(s_mutex);
    s_memory_cache.clear();
    s_initialized = false;
    std::printf("[DedSec Cache] Shutdown: %llu hits, %llu misses\n",
                static_cast<unsigned long long>(s_hits), static_cast<unsigned long long>(s_misses));
}

bool ShaderCache::lookup(const ShaderCacheKey& key, ShaderCacheEntry& out_entry) {
    if (!s_initialized) return false;

    uint64_t combined = fnv1a_combine(key);

    std::lock_guard lock(s_mutex);

    // Check memory cache first
    auto it = s_memory_cache.find(combined);
    if (it != s_memory_cache.end()) {
        out_entry = it->second;
        s_hits++;
        return true;
    }

    // Check disk
    auto path = cache_path(key);
    if (read_from_disk(path, out_entry)) {
        // Verify hash matches (defensive against corruption)
        if (out_entry.rdna2_hash == key.rdna2_hash && out_entry.options == key.options) {
            s_memory_cache[combined] = out_entry; // Promote to memory
            s_hits++;
            return true;
        }
    }

    s_misses++;
    return false;
}

void ShaderCache::store(const ShaderCacheKey& key, const std::vector<uint8_t>& dxil_blob, uint8_t stage, uint32_t options) {
    if (!s_initialized || dxil_blob.empty()) return;

    uint64_t combined = fnv1a_combine(key);

    ShaderCacheEntry entry;
    entry.dxil_blob = dxil_blob;
    entry.rdna2_hash = key.rdna2_hash;
    entry.options = options;
    entry.stage = stage;
    entry.creation_time = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    entry.driver_version = 0; // Set during initialize

    {
        std::lock_guard lock(s_mutex);
        s_memory_cache[combined] = entry;
    }

    // Write to disk (synchronous for simplicity; async would use a worker thread)
    auto path = cache_path(key);
    if (write_to_disk(path, entry)) {
        std::lock_guard lock(s_mutex);
        s_disk_entries++;
    }
}

void ShaderCache::warmup(const std::vector<ShaderCacheKey>& keys) {
    if (!s_initialized) return;

    std::lock_guard lock(s_mutex);
    uint64_t loaded = 0;

    for (const auto& key : keys) {
        uint64_t combined = fnv1a_combine(key);
        if (s_memory_cache.count(combined)) continue; // Already in memory

        auto path = cache_path(key);
        ShaderCacheEntry entry;
        if (read_from_disk(path, entry) && entry.rdna2_hash == key.rdna2_hash) {
            s_memory_cache[combined] = std::move(entry);
            loaded++;
        }
    }

    if (loaded > 0) {
        std::printf("[DedSec Cache] Warmup: loaded %llu entries into memory\n",
                    static_cast<unsigned long long>(loaded));
    }
}

uint64_t ShaderCache::get_hits() { return s_hits; }
uint64_t ShaderCache::get_misses() { return s_misses; }
uint64_t ShaderCache::get_disk_entries() { return s_disk_entries; }

uint64_t ShaderCache::get_memory_bytes() {
    std::lock_guard lock(s_mutex);
    uint64_t total = 0;
    for (const auto& [k, v] : s_memory_cache) {
        total += v.dxil_blob.size() + sizeof(ShaderCacheEntry);
    }
    return total;
}

void ShaderCache::clear() {
    std::lock_guard lock(s_mutex);
    s_memory_cache.clear();

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(s_cache_dir, ec)) {
        if (entry.path().extension() == ".dxil") {
            std::filesystem::remove(entry.path(), ec);
        }
    }
    s_disk_entries = 0;
    s_hits = 0;
    s_misses = 0;
    std::printf("[DedSec Cache] Cleared\n");
}

} // namespace dedsec