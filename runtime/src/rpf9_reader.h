// DedSec Fork — RPF9 Archive Reader
// Streaming reader for GTA VI RPF9 archives with GDeflate decompression.
// Clean-room implementation based on public format documentation.
//
// Design principles:
// - Streaming interface: never loads entire archive into memory
// - Budget-aware: max 64MB read buffer for GTX 1650 4GB VRAM target
// - Hash-based lookup via Jenkins One-At-A-Time (joaat)
// - Thread-safe: immutable after open, concurrent reads safe
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

namespace dedsec {
namespace vfs {

// ============================================================================
// RPF9 Format Constants
// ============================================================================

// RPF9 magic: "RPF9" stored as little-endian uint32 = 0x39465052 ("9FPR")
static constexpr uint32_t kRPF9Magic = 0x39465052;

// Maximum read buffer size (64MB budget for GTX 1650)
static constexpr size_t kMaxReadBuffer = 64 * 1024 * 1024;

// Default stream buffer for sequential reads (4MB)
static constexpr size_t kDefaultStreamBuffer = 4 * 1024 * 1024;

// ============================================================================
// RPF9 Header (32 bytes on disk)
// ============================================================================

#pragma pack(push, 1)
struct RPF9Header {
    uint32_t magic;           // 0x39465052 ("9FPR" LE)
    uint32_t version;         // Format version (typically 9)
    uint32_t entryCount;      // Number of entries in TOC
    uint32_t tocOffset;       // Offset to table of contents
    uint32_t tocSize;         // Size of TOC in bytes
    uint32_t dataOffset;      // Offset to data area start
    uint32_t dataSize;        // Total data area size
    uint32_t flags;           // Archive flags (encrypted TOC, etc.)
};
#pragma pack(pop)

static_assert(sizeof(RPF9Header) == 32, "RPF9Header must be 32 bytes");

// ============================================================================
// Entry Descriptor (in-memory representation)
// ============================================================================

struct RPF9Entry {
    uint32_t nameHash;        // joaat hash of the file path
    uint32_t offset;          // Offset within data area
    uint32_t compressedSize;  // Size on disk (compressed or raw)
    uint32_t uncompressedSize;// Original size (0 if not compressed)
    uint16_t flags;           // Compression type, encryption flags
    uint16_t padding;         // Alignment padding

    // Computed fields
    bool isCompressed() const { return (flags & 0x01) != 0; }
    bool isEncrypted() const { return (flags & 0x02) != 0; }
};

// ============================================================================
// Read Result
// ============================================================================

struct RPF9ReadResult {
    bool success;
    size_t bytesRead;
    const char* errorMessage;

    static RPF9ReadResult Ok(size_t bytes) { return {true, bytes, nullptr}; }
    static RPF9ReadResult Error(const char* msg) { return {false, 0, msg}; }
};

// ============================================================================
// Stream Callback (for streaming decompression without full buffer)
// ============================================================================

using StreamCallback = std::function<bool(const uint8_t* data, size_t size)>;

// ============================================================================
// RPF9 Reader Class
// ============================================================================

class RPF9Reader {
public:
    RPF9Reader();
    ~RPF9Reader();

    // Non-copyable, movable
    RPF9Reader(const RPF9Reader&) = delete;
    RPF9Reader& operator=(const RPF9Reader&) = delete;
    RPF9Reader(RPF9Reader&& other) noexcept;
    RPF9Reader& operator=(RPF9Reader&& other) noexcept;

    // Open an RPF9 archive file
    bool open(const char* path);
    bool open(const wchar_t* path);

    // Close and release resources
    void close();

    // Check if archive is open and valid
    bool isOpen() const { return m_file != nullptr; }

    // Get header info
    const RPF9Header& header() const { return m_header; }

    // Get entry count
    uint32_t entryCount() const { return m_header.entryCount; }

    // Lookup entry by joaat hash
    const RPF9Entry* findEntry(uint32_t nameHash) const;

    // Lookup entry by path string (computes joaat internally)
    const RPF9Entry* findEntry(const char* path) const;

    // Read entry data into caller-provided buffer
    // Returns bytes read, or 0 on error
    size_t readEntry(const RPF9Entry& entry, uint8_t* buffer, size_t bufferSize);

    // Read entry data with automatic decompression
    // Allocates output buffer if needed (caller must free with delete[])
    RPF9ReadResult readEntryDecompressed(const RPF9Entry& entry,
                                          uint8_t*& outBuffer,
                                          size_t& outSize);

    // Streaming read with callback (no intermediate buffer allocation)
    RPF9ReadResult readEntryStreaming(const RPF9Entry& entry, StreamCallback callback);

    // Read raw bytes at offset (for manual parsing)
    size_t readRaw(uint64_t offset, uint8_t* buffer, size_t size);

    // Compute joaat hash for a path string
    static uint32_t joaatHash(const char* str);
    static uint32_t joaatHash(const char* str, size_t len);

    // Get last error message
    const char* getLastError() const { return m_lastError; }

    // Statistics for debugging/profiling
    struct Stats {
        uint64_t totalBytesRead;
        uint64_t decompressionsPerformed;
        uint64_t cacheHits;
        uint64_t cacheMisses;
    };
    Stats getStats() const { return m_stats; }

private:
    FILE* m_file;
    RPF9Header m_header;
    std::vector<RPF9Entry> m_entries;
    const char* m_lastError;
    mutable Stats m_stats{};

    // Internal helpers
    bool parseHeader();
    bool parseTOC();
    size_t readAt(uint64_t offset, void* buffer, size_t size);
    bool decompressData(const uint8_t* compressed, size_t compSize,
                        uint8_t* output, size_t outputSize);
};

} // namespace vfs
} // namespace dedsec