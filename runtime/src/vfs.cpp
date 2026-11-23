// DedSec Fork — Virtual File System Layer Implementation
// Path mapping, case-insensitive lookup, overlay support, and RPF9 integration.
// Clean-room implementation for Xbox→PC path translation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace dedsec {
namespace vfs {

// ============================================================================
// Construction / Destruction
// ============================================================================

VirtualFileSystem::VirtualFileSystem()
    : m_initialized(false), m_stats{} {}

VirtualFileSystem::~VirtualFileSystem() {
    shutdown();
}

// ============================================================================
// Initialize / Shutdown
// ============================================================================

bool VirtualFileSystem::initialize(const VFSConfig& config) {
    std::lock_guard lock(m_mutex);

    if (m_initialized) {
        return true; // Already initialized
    }

    m_config = config;

    // Validate game directory exists
    if (!m_config.gameDir.empty()) {
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(m_config.gameDir.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            std::fprintf(stderr, "[rt] vfs: game directory not found: %s\n", m_config.gameDir.c_str());
            // Don't fail — archives may be mounted later
        }
#else
        struct stat st{};
        if (stat(m_config.gameDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            std::fprintf(stderr, "[rt] vfs: game directory not found: %s\n", m_config.gameDir.c_str());
        }
#endif
    }

    m_initialized = true;
    std::fprintf(stderr, "[rt] vfs: initialized (game=%s, overlay=%s)\n",
                 m_config.gameDir.c_str(),
                 m_config.enableOverlay ? m_config.overlayDir.c_str() : "(disabled)");
    return true;
}

void VirtualFileSystem::shutdown() {
    std::lock_guard lock(m_mutex);

    m_archives.clear();
    m_cache.clear();
    m_initialized = false;
    m_stats = {};
}

// ============================================================================
// Archive Management
// ============================================================================

bool VirtualFileSystem::mountArchive(const char* archivePath) {
    if (!archivePath) return false;

    std::lock_guard lock(m_mutex);

    if (!m_initialized) return false;

    // Check if already mounted
    for (const auto& arch : m_archives) {
        if (arch.path == archivePath) return true;
    }

    // Budget check
    if (m_archives.size() >= m_config.maxOpenArchives) {
        std::fprintf(stderr, "[rt] vfs: max open archives reached (%u)\n", m_config.maxOpenArchives);
        return false;
    }

    MountedArchive ma;
    ma.path = archivePath;

    if (!ma.reader.open(archivePath)) {
        std::fprintf(stderr, "[rt] vfs: failed to mount archive: %s (%s)\n",
                     archivePath, ma.reader.getLastError());
        return false;
    }

    m_archives.push_back(std::move(ma));
    std::fprintf(stderr, "[rt] vfs: mounted archive: %s (%u entries)\n",
                 archivePath, m_archives.back().reader.entryCount());
    return true;
}

bool VirtualFileSystem::unmountArchive(const char* archivePath) {
    if (!archivePath) return false;

    std::lock_guard lock(m_mutex);

    for (auto it = m_archives.begin(); it != m_archives.end(); ++it) {
        if (it->path == archivePath) {
            it->reader.close();
            m_archives.erase(it);

            // Invalidate cache entries from this archive
            size_t idx = static_cast<size_t>(std::distance(m_archives.begin(), it));
            for (auto cit = m_cache.begin(); cit != m_cache.end();) {
                if (cit->second.archiveIndex == idx) {
                    cit = m_cache.erase(cit);
                } else {
                    // Adjust indices above the removed one
                    if (cit->second.archiveIndex > idx && cit->second.archiveIndex != SIZE_MAX) {
                        cit->second.archiveIndex--;
                    }
                    ++cit;
                }
            }
            return true;
        }
    }
    return false;
}

size_t VirtualFileSystem::archiveCount() const {
    std::lock_guard lock(m_mutex);
    return m_archives.size();
}

// ============================================================================
// Path Normalization
// ============================================================================

std::string VirtualFileSystem::normalizePath(const char* path, bool toLower) {
    if (!path) return {};

    std::string result(path);

    // Normalize separators: backslash → forward slash
    for (char& c : result) {
        if (c == '\\') c = '/';
    }

    // Strip leading "game:/" or "device:/" prefixes (Xbox virtual drives)
    const char* prefixes[] = {"game:/", "device:/", "title:/", "update:/"};
    for (const char* prefix : prefixes) {
        size_t plen = std::strlen(prefix);
        if (result.size() >= plen) {
            std::string head = result.substr(0, plen);
            if (toLower) {
                for (char& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (head == prefix) {
                result = result.substr(plen);
                break;
            }
        }
    }

    // Remove trailing slashes
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }

    // Collapse double slashes
    std::string cleaned;
    cleaned.reserve(result.size());
    bool lastWasSlash = false;
    for (char c : result) {
        if (c == '/') {
            if (!lastWasSlash) cleaned += c;
            lastWasSlash = true;
        } else {
            cleaned += c;
            lastWasSlash = false;
        }
    }

    if (toLower) {
        for (char& c : cleaned) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return cleaned;
}

std::string VirtualFileSystem::xboxToPcPath(const char* xboxPath, const std::string& gameDir) {
    std::string normalized = normalizePath(xboxPath, false);

    if (gameDir.empty()) return normalized;

    // Ensure gameDir ends with separator
    std::string base = gameDir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') {
        base += '/';
    }

    return base + normalized;
}

// ============================================================================
// Internal Path Checks
// ============================================================================

std::string VirtualFileSystem::buildOverlayPath(const char* normalizedPath) const {
    if (m_config.overlayDir.empty()) return {};

    std::string base = m_config.overlayDir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') {
        base += '/';
    }
    return base + normalizedPath;
}

std::string VirtualFileSystem::buildFilesystemPath(const char* normalizedPath) const {
    return xboxToPcPath(normalizedPath, m_config.gameDir);
}

bool VirtualFileSystem::checkOverlay(const char* normalizedPath, VFSEntryInfo& outInfo) {
    if (!m_config.enableOverlay || m_config.overlayDir.empty()) return false;

    std::string overlayPath = buildOverlayPath(normalizedPath);

#ifdef _WIN32
    DWORD attr = GetFileAttributesA(overlayPath.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExA(overlayPath.c_str(), GetFileExInfoStandard, &fad)) {
            LARGE_INTEGER size;
            size.HighPart = fad.nFileSizeHigh;
            size.LowPart = fad.nFileSizeLow;
            outInfo.exists = true;
            outInfo.isArchiveEntry = false;
            outInfo.isOverlayEntry = true;
            outInfo.size = static_cast<size_t>(size.QuadPart);
            outInfo.uncompressedSize = outInfo.size;
            outInfo.archivePath = nullptr;
            return true;
        }
    }
#else
    struct stat st{};
    if (stat(overlayPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        outInfo.exists = true;
        outInfo.isArchiveEntry = false;
        outInfo.isOverlayEntry = true;
        outInfo.size = static_cast<size_t>(st.st_size);
        outInfo.uncompressedSize = outInfo.size;
        outInfo.archivePath = nullptr;
        return true;
    }
#endif

    return false;
}

bool VirtualFileSystem::checkArchives(uint32_t pathHash, VFSEntryInfo& outInfo,
                                       size_t& archiveIdx, RPF9Entry& rpfEntry) {
    for (size_t i = 0; i < m_archives.size(); ++i) {
        const RPF9Entry* entry = m_archives[i].reader.findEntry(pathHash);
        if (entry) {
            outInfo.exists = true;
            outInfo.isArchiveEntry = true;
            outInfo.isOverlayEntry = false;
            outInfo.size = entry->compressedSize;
            outInfo.uncompressedSize = entry->uncompressedSize > 0 ? entry->uncompressedSize : entry->compressedSize;
            outInfo.archivePath = m_archives[i].path.c_str();
            archiveIdx = i;
            rpfEntry = *entry;
            return true;
        }
    }
    return false;
}

bool VirtualFileSystem::checkFilesystem(const char* pcPath, VFSEntryInfo& outInfo) {
    if (!pcPath || pcPath[0] == '\0') return false;

#ifdef _WIN32
    DWORD attr = GetFileAttributesA(pcPath);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExA(pcPath, GetFileExInfoStandard, &fad)) {
            LARGE_INTEGER size;
            size.HighPart = fad.nFileSizeHigh;
            size.LowPart = fad.nFileSizeLow;
            outInfo.exists = true;
            outInfo.isArchiveEntry = false;
            outInfo.isOverlayEntry = false;
            outInfo.size = static_cast<size_t>(size.QuadPart);
            outInfo.uncompressedSize = outInfo.size;
            outInfo.archivePath = nullptr;
            return true;
        }
    }
#else
    struct stat st{};
    if (stat(pcPath, &st) == 0 && S_ISREG(st.st_mode)) {
        outInfo.exists = true;
        outInfo.isArchiveEntry = false;
        outInfo.isOverlayEntry = false;
        outInfo.size = static_cast<size_t>(st.st_size);
        outInfo.uncompressedSize = outInfo.size;
        outInfo.archivePath = nullptr;
        return true;
    }
#endif

    return false;
}

// ============================================================================
// Path Resolution
// ============================================================================

bool VirtualFileSystem::resolvePath(const char* virtualPath, VFSEntryInfo& outInfo) {
    if (!virtualPath || !m_initialized) {
        outInfo = {};
        return false;
    }

    std::lock_guard lock(m_mutex);
    m_stats.pathResolutions++;

    std::string normalized = normalizePath(virtualPath, m_config.caseInsensitive);
    uint32_t hash = RPF9Reader::joaatHash(normalized.c_str());

    // Check cache first
    auto cacheIt = m_cache.find(hash);
    if (cacheIt != m_cache.end()) {
        outInfo = cacheIt->second.info;
        return outInfo.exists;
    }

    // Priority order: overlay → archives → filesystem
    CachedEntry cached{};
    cached.archiveIndex = SIZE_MAX;

    if (checkOverlay(normalized.c_str(), outInfo)) {
        m_stats.overlayHits++;
        cached.info = outInfo;
        m_cache[hash] = cached;
        return true;
    }

    size_t archiveIdx = SIZE_MAX;
    RPF9Entry rpfEntry{};
    if (checkArchives(hash, outInfo, archiveIdx, rpfEntry)) {
        m_stats.archiveHits++;
        cached.info = outInfo;
        cached.archiveIndex = archiveIdx;
        cached.rpfEntry = rpfEntry;
        m_cache[hash] = cached;
        return true;
    }

    std::string fsPath = buildFilesystemPath(normalized.c_str());
    if (checkFilesystem(fsPath.c_str(), outInfo)) {
        m_stats.filesystemHits++;
        cached.info = outInfo;
        m_cache[hash] = cached;
        return true;
    }

    m_stats.misses++;
    outInfo = {};
    return false;
}

bool VirtualFileSystem::fileExists(const char* virtualPath) {
    VFSEntryInfo info{};
    return resolvePath(virtualPath, info);
}

// ============================================================================
// File Reading
// ============================================================================

VFSReadResult VirtualFileSystem::readFile(const char* virtualPath, uint8_t* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) return VFSReadResult::Error("invalid buffer");

    VFSEntryInfo info{};
    if (!resolvePath(virtualPath, info)) {
        return VFSReadResult::Error("file not found");
    }

    std::lock_guard lock(m_mutex);

    // Overlay file: direct filesystem read
    if (info.isOverlayEntry) {
        std::string normalized = normalizePath(virtualPath, m_config.caseInsensitive);
        std::string overlayPath = buildOverlayPath(normalized.c_str());

        FILE* f = std::fopen(overlayPath.c_str(), "rb");
        if (!f) return VFSReadResult::Error("failed to open overlay file");

        size_t toRead = std::min(bufferSize, info.size);
        size_t bytesRead = std::fread(buffer, 1, toRead, f);
        std::fclose(f);
        m_stats.totalBytesRead += bytesRead;
        return VFSReadResult::Ok(bytesRead);
    }

    // Archive entry: read via RPF9 reader
    if (info.isArchiveEntry) {
        uint32_t hash = RPF9Reader::joaatHash(normalizePath(virtualPath, m_config.caseInsensitive).c_str());
        auto cacheIt = m_cache.find(hash);
        if (cacheIt == m_cache.end() || cacheIt->second.archiveIndex >= m_archives.size()) {
            return VFSReadResult::Error("cache inconsistency");
        }

        size_t idx = cacheIt->second.archiveIndex;
        size_t bytesRead = m_archives[idx].reader.readEntry(cacheIt->second.rpfEntry, buffer, bufferSize);
        m_stats.totalBytesRead += bytesRead;
        return VFSReadResult::Ok(bytesRead);
    }

    // Plain filesystem file
    std::string normalized = normalizePath(virtualPath, m_config.caseInsensitive);
    std::string fsPath = buildFilesystemPath(normalized.c_str());

    FILE* f = std::fopen(fsPath.c_str(), "rb");
    if (!f) return VFSReadResult::Error("failed to open file");

    size_t toRead = std::min(bufferSize, info.size);
    size_t bytesRead = std::fread(buffer, 1, toRead, f);
    std::fclose(f);
    m_stats.totalBytesRead += bytesRead;
    return VFSReadResult::Ok(bytesRead);
}

VFSReadResult VirtualFileSystem::readFileDecompressed(const char* virtualPath,
                                                       uint8_t*& outBuffer,
                                                       size_t& outSize) {
    VFSEntryInfo info{};
    if (!resolvePath(virtualPath, info)) {
        return VFSReadResult::Error("file not found");
    }

    std::lock_guard lock(m_mutex);

    // Non-archive files are already uncompressed
    if (!info.isArchiveEntry) {
        outSize = info.size;
        outBuffer = new (std::nothrow) uint8_t[outSize];
        if (!outBuffer) return VFSReadResult::Error("allocation failed");

        auto result = readFile(virtualPath, outBuffer, outSize);
        if (!result.success) {
            delete[] outBuffer;
            outBuffer = nullptr;
            outSize = 0;
            return result;
        }
        return result;
    }

    // Archive entry: use RPF9 reader's decompression
    uint32_t hash = RPF9Reader::joaatHash(normalizePath(virtualPath, m_config.caseInsensitive).c_str());
    auto cacheIt = m_cache.find(hash);
    if (cacheIt == m_cache.end() || cacheIt->second.archiveIndex >= m_archives.size()) {
        return VFSReadResult::Error("cache inconsistency");
    }

    size_t idx = cacheIt->second.archiveIndex;
    auto rpfResult = m_archives[idx].reader.readEntryDecompressed(
        cacheIt->second.rpfEntry, outBuffer, outSize);

    if (rpfResult.success) {
        m_stats.totalBytesRead += rpfResult.bytesRead;
    }
    return {rpfResult.success, rpfResult.bytesRead, rpfResult.errorMessage};
}

VFSReadResult VirtualFileSystem::readFileStreaming(const char* virtualPath, StreamCallback callback) {
    if (!callback) return VFSReadResult::Error("null callback");

    VFSEntryInfo info{};
    if (!resolvePath(virtualPath, info)) {
        return VFSReadResult::Error("file not found");
    }

    std::lock_guard lock(m_mutex);

    // For non-archive files, stream from filesystem
    if (!info.isArchiveEntry) {
        std::string normalized = normalizePath(virtualPath, m_config.caseInsensitive);
        std::string filePath = info.isOverlayEntry
                                   ? buildOverlayPath(normalized.c_str())
                                   : buildFilesystemPath(normalized.c_str());

        FILE* f = std::fopen(filePath.c_str(), "rb");
        if (!f) return VFSReadResult::Error("failed to open file");

        constexpr size_t kChunkSize = 64 * 1024;
        uint8_t chunk[kChunkSize];
        size_t totalRead = 0;

        while (true) {
            size_t bytesRead = std::fread(chunk, 1, kChunkSize, f);
            if (bytesRead == 0) break;
            if (!callback(chunk, bytesRead)) break;
            totalRead += bytesRead;
        }

        std::fclose(f);
        m_stats.totalBytesRead += totalRead;
        return VFSReadResult::Ok(totalRead);
    }

    // Archive entry: delegate to RPF9 reader streaming
    uint32_t hash = RPF9Reader::joaatHash(normalizePath(virtualPath, m_config.caseInsensitive).c_str());
    auto cacheIt = m_cache.find(hash);
    if (cacheIt == m_cache.end() || cacheIt->second.archiveIndex >= m_archives.size()) {
        return VFSReadResult::Error("cache inconsistency");
    }

    size_t idx = cacheIt->second.archiveIndex;
    auto rpfResult = m_archives[idx].reader.readEntryStreaming(cacheIt->second.rpfEntry, callback);
    if (rpfResult.success) {
        m_stats.totalBytesRead += rpfResult.bytesRead;
    }
    return {rpfResult.success, rpfResult.bytesRead, rpfResult.errorMessage};
}

// ============================================================================
// Statistics
// ============================================================================

VirtualFileSystem::Stats VirtualFileSystem::getStats() const {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

void VirtualFileSystem::resetStats() {
    std::lock_guard lock(m_mutex);
    m_stats = {};
}

// ============================================================================
// Global Singleton
// ============================================================================

VirtualFileSystem& getGlobalVFS() {
    static VirtualFileSystem instance;
    return instance;
}

} // namespace vfs
} // namespace dedsec