// DedSec Fork — Virtual File System Layer
// Path mapping, case-insensitive lookup, overlay support, and RPF9 integration.
// Clean-room implementation for Xbox→PC path translation.
//
// Design principles:
// - Thread-safe: SRW lock protects mutable state
// - Overlay-first: modded files override base archive entries
// - Case-insensitive: matches Xbox filesystem behavior
// - Budget-aware: no unbounded allocations
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include "rpf9_reader.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace dedsec {
namespace vfs {

// ============================================================================
// VFS Configuration
// ============================================================================

struct VFSConfig {
    // Base game directory (where RPF archives live)
    std::string gameDir;

    // Overlay directory for mods (checked first)
    std::string overlayDir;

    // Maximum number of open archives
    uint32_t maxOpenArchives = 16;

    // Enable case-insensitive path resolution
    bool caseInsensitive = true;

    // Enable overlay support
    bool enableOverlay = true;
};

// ============================================================================
// VFS Entry Info
// ============================================================================

struct VFSEntryInfo {
    bool exists;
    bool isArchiveEntry;   // True if inside an RPF archive
    bool isOverlayEntry;   // True if from overlay directory
    size_t size;           // File size (compressed if in archive)
    size_t uncompressedSize;
    const char* archivePath; // Which archive contains this file (if applicable)
};

// ============================================================================
// VFS Read Result
// ============================================================================

struct VFSReadResult {
    bool success;
    size_t bytesRead;
    const char* errorMessage;

    static VFSReadResult Ok(size_t bytes) { return {true, bytes, nullptr}; }
    static VFSReadResult Error(const char* msg) { return {false, 0, msg}; }
};

// ============================================================================
// VFS Class
// ============================================================================

class VirtualFileSystem {
public:
    VirtualFileSystem();
    ~VirtualFileSystem();

    // Non-copyable
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    // Initialize with configuration
    bool initialize(const VFSConfig& config);

    // Shutdown and release all resources
    void shutdown();

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

    // ========================================================================
    // Archive Management
    // ========================================================================

    // Mount an RPF9 archive
    bool mountArchive(const char* archivePath);

    // Unmount an archive by path
    bool unmountArchive(const char* archivePath);

    // Get number of mounted archives
    size_t archiveCount() const;

    // ========================================================================
    // Path Resolution
    // ========================================================================

    // Resolve a virtual path to physical location
    // Returns true if found, fills outInfo
    bool resolvePath(const char* virtualPath, VFSEntryInfo& outInfo);

    // Check if a file exists (overlay → archive → filesystem)
    bool fileExists(const char* virtualPath);

    // ========================================================================
    // File Reading
    // ========================================================================

    // Read entire file into caller-provided buffer
    VFSReadResult readFile(const char* virtualPath, uint8_t* buffer, size_t bufferSize);

    // Read file with automatic decompression (allocates output)
    VFSReadResult readFileDecompressed(const char* virtualPath,
                                        uint8_t*& outBuffer,
                                        size_t& outSize);

    // Streaming read with callback
    VFSReadResult readFileStreaming(const char* virtualPath, StreamCallback callback);

    // ========================================================================
    // Path Mapping (Xbox → PC)
    // ========================================================================

    // Convert Xbox-style path to PC local path
    // e.g., "game:\levels\map.rpf" → "C:\Games\GTA6\levels\map.rpf"
    static std::string xboxToPcPath(const char* xboxPath, const std::string& gameDir);

    // Normalize path separators and case
    static std::string normalizePath(const char* path, bool toLower = true);

    // Compute joaat hash (delegates to RPF9Reader)
    static uint32_t pathHash(const char* path) { return RPF9Reader::joaatHash(path); }

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Stats {
        uint64_t pathResolutions;
        uint64_t overlayHits;
        uint64_t archiveHits;
        uint64_t filesystemHits;
        uint64_t misses;
        uint64_t totalBytesRead;
    };

    Stats getStats() const;
    void resetStats();

private:
    bool m_initialized;
    VFSConfig m_config;
    mutable std::mutex m_mutex; // Protects all mutable state below

    // Mounted archives (path → reader)
    struct MountedArchive {
        std::string path;
        RPF9Reader reader;
    };
    std::vector<MountedArchive> m_archives;

    // Path cache for resolved lookups (hash → info)
    struct CachedEntry {
        VFSEntryInfo info;
        size_t archiveIndex; // Index into m_archives, or SIZE_MAX for overlay/fs
        RPF9Entry rpfEntry;  // Valid only if isArchiveEntry
    };
    std::unordered_map<uint32_t, CachedEntry> m_cache;

    mutable Stats m_stats{};

    // Internal helpers
    bool checkOverlay(const char* normalizedPath, VFSEntryInfo& outInfo);
    bool checkArchives(uint32_t pathHash, VFSEntryInfo& outInfo, size_t& archiveIdx, RPF9Entry& rpfEntry);
    bool checkFilesystem(const char* pcPath, VFSEntryInfo& outInfo);
    std::string buildOverlayPath(const char* normalizedPath) const;
    std::string buildFilesystemPath(const char* normalizedPath) const;
};

// ============================================================================
// Global VFS Instance (singleton pattern for runtime use)
// ============================================================================

VirtualFileSystem& getGlobalVFS();

} // namespace vfs
} // namespace dedsec