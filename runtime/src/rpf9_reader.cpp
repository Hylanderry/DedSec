// DedSec Fork — RPF9 Archive Reader Implementation
// Streaming reader for GTA VI RPF9 archives with GDeflate decompression.
// Clean-room implementation based on public format documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "rpf9_reader.h"
#include "../shims/gdeflate_decompressor.h"

#include <algorithm>
#include <cstring>
#include <cwchar>

namespace dedsec {
namespace vfs {

// ============================================================================
// Jenkins One-At-A-Time Hash (joaat)
// Standard hash used by RAGE engine for path lookups.
// ============================================================================

uint32_t RPF9Reader::joaatHash(const char* str) {
    if (!str) return 0;
    uint32_t hash = 0;
    while (*str) {
        // Lowercase for case-insensitive lookup
        char c = *str++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        // Backslash to forward slash normalization
        if (c == '\\') c = '/';
        hash += static_cast<uint8_t>(c);
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

uint32_t RPF9Reader::joaatHash(const char* str, size_t len) {
    if (!str) return 0;
    uint32_t hash = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '\\') c = '/';
        hash += static_cast<uint8_t>(c);
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

// ============================================================================
// Construction / Destruction / Move
// ============================================================================

RPF9Reader::RPF9Reader()
    : m_file(nullptr), m_header{}, m_lastError(nullptr) {
    std::memset(&m_header, 0, sizeof(m_header));
}

RPF9Reader::~RPF9Reader() {
    close();
}

RPF9Reader::RPF9Reader(RPF9Reader&& other) noexcept
    : m_file(other.m_file),
      m_header(other.m_header),
      m_entries(std::move(other.m_entries)),
      m_lastError(other.m_lastError),
      m_stats(other.m_stats) {
    other.m_file = nullptr;
    other.m_lastError = nullptr;
    std::memset(&other.m_header, 0, sizeof(other.m_header));
    other.m_stats = {};
}

RPF9Reader& RPF9Reader::operator=(RPF9Reader&& other) noexcept {
    if (this != &other) {
        close();
        m_file = other.m_file;
        m_header = other.m_header;
        m_entries = std::move(other.m_entries);
        m_lastError = other.m_lastError;
        m_stats = other.m_stats;
        other.m_file = nullptr;
        other.m_lastError = nullptr;
        std::memset(&other.m_header, 0, sizeof(other.m_header));
        other.m_stats = {};
    }
    return *this;
}

// ============================================================================
// Open / Close
// ============================================================================

bool RPF9Reader::open(const char* path) {
    if (!path) {
        m_lastError = "null path";
        return false;
    }
    close();

#ifdef _WIN32
    // Convert UTF-8 to wide for Windows fopen
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen <= 0) {
        m_lastError = "invalid UTF-8 path";
        return false;
    }
    std::vector<wchar_t> wpath(static_cast<size_t>(wlen));
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), wlen);
    m_file = _wfopen(wpath.data(), L"rb");
#else
    m_file = std::fopen(path, "rb");
#endif

    if (!m_file) {
        m_lastError = "failed to open file";
        return false;
    }

    if (!parseHeader()) {
        // Preserve m_lastError set by parseHeader before close() clears it.
        const char* err = m_lastError;
        close();
        m_lastError = err;
        return false;
    }

    if (!parseTOC()) {
        const char* err = m_lastError;
        close();
        m_lastError = err;
        return false;
    }

    return true;
}

bool RPF9Reader::open(const wchar_t* path) {
    if (!path) {
        m_lastError = "null path";
        return false;
    }
    close();

    m_file = _wfopen(path, L"rb");
    if (!m_file) {
        m_lastError = "failed to open file";
        return false;
    }

    if (!parseHeader()) {
        const char* err = m_lastError;
        close();
        m_lastError = err;
        return false;
    }

    if (!parseTOC()) {
        const char* err = m_lastError;
        close();
        m_lastError = err;
        return false;
    }

    return true;
}

void RPF9Reader::close() {
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_entries.clear();
    std::memset(&m_header, 0, sizeof(m_header));
    m_lastError = nullptr;
}

// ============================================================================
// Internal: Read at offset
// ============================================================================

size_t RPF9Reader::readAt(uint64_t offset, void* buffer, size_t size) {
    if (!m_file || !buffer || size == 0) return 0;

#ifdef _WIN32
    _fseeki64(m_file, static_cast<__int64>(offset), SEEK_SET);
#else
    std::fseek(m_file, static_cast<long>(offset), SEEK_SET);
#endif

    size_t bytesRead = std::fread(buffer, 1, size, m_file);
    m_stats.totalBytesRead += bytesRead;
    return bytesRead;
}

size_t RPF9Reader::readRaw(uint64_t offset, uint8_t* buffer, size_t size) {
    return readAt(offset, buffer, size);
}

// ============================================================================
// Parse Header
// ============================================================================

bool RPF9Reader::parseHeader() {
    size_t bytesRead = readAt(0, &m_header, sizeof(m_header));
    if (bytesRead != sizeof(m_header)) {
        m_lastError = "failed to read RPF9 header";
        return false;
    }

    if (m_header.magic != kRPF9Magic) {
        m_lastError = "invalid RPF9 magic";
        return false;
    }

    // Sanity checks
    if (m_header.entryCount > 10000000) {
        m_lastError = "entry count exceeds sanity limit";
        return false;
    }

    if (m_header.tocOffset < sizeof(RPF9Header)) {
        m_lastError = "TOC offset before header end";
        return false;
    }

    return true;
}

// ============================================================================
// Parse Table of Contents
// Note: The TOC in retail RPF9 archives is encrypted/packed.
// This implementation reads the raw TOC structure assuming it has been
// decrypted externally or the archive uses an unencrypted layout.
// For encrypted archives, this will fail gracefully.
// ============================================================================

bool RPF9Reader::parseTOC() {
    if (m_header.entryCount == 0) {
        return true; // Empty archive is valid
    }

    // Reserve space for entries
    m_entries.reserve(m_header.entryCount);

    // Read TOC entries. Each entry is 16 bytes in the standard layout.
    constexpr size_t kEntrySize = 16;
    size_t tocBytesNeeded = static_cast<size_t>(m_header.entryCount) * kEntrySize;

    // Budget check: don't allocate more than 64MB for TOC
    if (tocBytesNeeded > kMaxReadBuffer) {
        m_lastError = "TOC size exceeds memory budget";
        return false;
    }

    // Read TOC in chunks to avoid large single allocation
    std::vector<uint8_t> tocBuffer(tocBytesNeeded);
    size_t bytesRead = readAt(m_header.tocOffset, tocBuffer.data(), tocBytesNeeded);
    if (bytesRead != tocBytesNeeded) {
        m_lastError = "failed to read TOC";
        return false;
    }

    // Parse each entry
    for (uint32_t i = 0; i < m_header.entryCount; ++i) {
        const uint8_t* entryData = tocBuffer.data() + static_cast<size_t>(i) * kEntrySize;
        RPF9Entry entry{};

        // Little-endian field reads
        std::memcpy(&entry.nameHash, entryData + 0, 4);
        std::memcpy(&entry.offset, entryData + 4, 4);
        std::memcpy(&entry.compressedSize, entryData + 8, 4);

        // Upper bits of compressedSize may encode flags in some variants
        entry.uncompressedSize = 0;
        entry.flags = 0;
        entry.padding = 0;

        // Check if there's extended entry data (uncompressed size + flags)
        if (kEntrySize >= 16) {
            std::memcpy(&entry.uncompressedSize, entryData + 12, 4);
        }

        // Heuristic: if uncompressedSize > compressedSize and compressedSize > 0,
        // the entry is likely compressed
        if (entry.uncompressedSize > 0 && entry.uncompressedSize > entry.compressedSize) {
            entry.flags |= 0x01; // Mark as compressed
        }

        m_entries.push_back(entry);
    }

    // Sort by hash for binary search lookup
    std::sort(m_entries.begin(), m_entries.end(),
              [](const RPF9Entry& a, const RPF9Entry& b) {
                  return a.nameHash < b.nameHash;
              });

    return true;
}

// ============================================================================
// Entry Lookup
// ============================================================================

const RPF9Entry* RPF9Reader::findEntry(uint32_t nameHash) const {
    if (m_entries.empty()) return nullptr;

    // Binary search on sorted entries
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), nameHash,
                               [](const RPF9Entry& entry, uint32_t hash) {
                                   return entry.nameHash < hash;
                               });

    if (it != m_entries.end() && it->nameHash == nameHash) {
        m_stats.cacheHits++;
        return &(*it);
    }

    m_stats.cacheMisses++;
    return nullptr;
}

const RPF9Entry* RPF9Reader::findEntry(const char* path) const {
    if (!path) return nullptr;
    return findEntry(joaatHash(path));
}

// ============================================================================
// Decompression Helper
// ============================================================================

bool RPF9Reader::decompressData(const uint8_t* compressed, size_t compSize,
                                 uint8_t* output, size_t outputSize) {
    // Try GDeflate first (Xbox Series native format)
    if (compression::GDeflateDecompressor::isGDeflateCompressed(compressed, compSize)) {
        compression::GDeflateDecompressor decompressor;
        if (!decompressor.initialize()) return false;

        auto result = decompressor.decompress(compressed, compSize, output, outputSize);
        if (result.success) {
            m_stats.decompressionsPerformed++;
            return true;
        }
    }

    // Fallback: try zlib (most RPF9 data is plain zlib per docs/rpf.md)
    // Simple zlib detection: first byte 0x78 followed by valid CMF/FLG
    if (compSize >= 2 && compressed[0] == 0x78) {
        // Use Windows built-in inflate via RtlDecompressBuffer or similar
        // For now, mark as needing external zlib support
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "[rt] rpf9: zlib decompression not yet integrated; "
                         "GDeflate-only for now\n");
            warned = true;
        }
        return false;
    }

    return false;
}

// ============================================================================
// Read Entry Data
// ============================================================================

size_t RPF9Reader::readEntry(const RPF9Entry& entry, uint8_t* buffer, size_t bufferSize) {
    if (!m_file || !buffer) return 0;

    size_t toRead = std::min(bufferSize, static_cast<size_t>(entry.compressedSize));
    uint64_t absOffset = m_header.dataOffset + entry.offset;

    return readAt(absOffset, buffer, toRead);
}

RPF9ReadResult RPF9Reader::readEntryDecompressed(const RPF9Entry& entry,
                                                   uint8_t*& outBuffer,
                                                   size_t& outSize) {
    if (!m_file) return RPF9ReadResult::Error("archive not open");

    // Uncompressed entry: direct read
    if (!entry.isCompressed() || entry.uncompressedSize == 0) {
        outSize = entry.compressedSize;
        outBuffer = new (std::nothrow) uint8_t[outSize];
        if (!outBuffer) return RPF9ReadResult::Error("allocation failed");

        size_t bytesRead = readEntry(entry, outBuffer, outSize);
        if (bytesRead != outSize) {
            delete[] outBuffer;
            outBuffer = nullptr;
            return RPF9ReadResult::Error("short read");
        }
        return RPF9ReadResult::Ok(bytesRead);
    }

    // Compressed entry: read compressed data, then decompress
    if (entry.uncompressedSize > kMaxReadBuffer) {
        return RPF9ReadResult::Error("uncompressed size exceeds memory budget");
    }

    // Allocate compressed buffer
    std::vector<uint8_t> compBuf(entry.compressedSize);
    size_t bytesRead = readEntry(entry, compBuf.data(), entry.compressedSize);
    if (bytesRead != entry.compressedSize) {
        return RPF9ReadResult::Error("short read on compressed data");
    }

    // Allocate output buffer
    outSize = entry.uncompressedSize;
    outBuffer = new (std::nothrow) uint8_t[outSize];
    if (!outBuffer) return RPF9ReadResult::Error("allocation failed");

    // Decompress
    if (!decompressData(compBuf.data(), compBuf.size(), outBuffer, outSize)) {
        delete[] outBuffer;
        outBuffer = nullptr;
        outSize = 0;
        return RPF9ReadResult::Error("decompression failed");
    }

    return RPF9ReadResult::Ok(outSize);
}

RPF9ReadResult RPF9Reader::readEntryStreaming(const RPF9Entry& entry, StreamCallback callback) {
    if (!m_file) return RPF9ReadResult::Error("archive not open");
    if (!callback) return RPF9ReadResult::Error("null callback");

    // For uncompressed data, stream directly from file
    if (!entry.isCompressed()) {
        constexpr size_t kChunkSize = 64 * 1024; // 64KB chunks
        uint8_t chunk[kChunkSize];
        size_t remaining = entry.compressedSize;
        uint64_t absOffset = m_header.dataOffset + entry.offset;
        size_t totalRead = 0;

        while (remaining > 0) {
            size_t toRead = std::min(remaining, kChunkSize);
            size_t bytesRead = readAt(absOffset + totalRead, chunk, toRead);
            if (bytesRead == 0) return RPF9ReadResult::Error("stream read failed");

            if (!callback(chunk, bytesRead)) {
                // Callback already accumulated these bytes internally;
                // include them in our total before returning.
                return RPF9ReadResult::Ok(totalRead + bytesRead);
            }

            totalRead += bytesRead;
            remaining -= bytesRead;
        }
        return RPF9ReadResult::Ok(totalRead);
    }

    // For compressed data, we need to decompress in chunks.
    // GDeflate doesn't support true streaming, so we decompress fully
    // but deliver via callback to avoid holding both buffers simultaneously.
    uint8_t* decompressed = nullptr;
    size_t decompSize = 0;
    auto result = readEntryDecompressed(entry, decompressed, decompSize);
    if (!result.success) return result;

    // Deliver via callback in chunks
    constexpr size_t kChunkSize = 64 * 1024;
    size_t delivered = 0;
    while (delivered < decompSize) {
        size_t chunkLen = std::min(decompSize - delivered, kChunkSize);
        if (!callback(decompressed + delivered, chunkLen)) break;
        delivered += chunkLen;
    }

    delete[] decompressed;
    return RPF9ReadResult::Ok(delivered);
}

} // namespace vfs
} // namespace dedsec