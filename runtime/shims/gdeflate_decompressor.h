// DedSec Fork — GDeflate Decompressor
// Xbox Series asset compression format (Microsoft proprietary)
// Clean-room implementation based on public documentation and behavior analysis.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace dedsec {
namespace compression {

// GDeflate compression levels used by Xbox Series
enum class GDeflateLevel : uint8_t {
    None = 0,       // No compression (passthrough)
    Fastest = 1,    // Level 1-3 equivalent
    Default = 6,    // Level 4-6 equivalent
    Best = 9        // Level 7-9 equivalent
};

// GDeflate frame header (16 bytes)
struct GDeflateFrameHeader {
    uint32_t magic;             // 0x4445464C ("DEFL") or variant
    uint16_t version;           // Format version (typically 1)
    uint16_t flags;             // Compression flags
    uint32_t uncompressedSize;  // Original data size
    uint32_t compressedSize;    // Compressed payload size
};

// GDeflate decompression result
struct GDeflateResult {
    bool success;
    size_t bytesDecompressed;
    const char* errorMessage;
};

// Main decompressor class
class GDeflateDecompressor {
public:
    GDeflateDecompressor();
    ~GDeflateDecompressor();

    // Initialize decompressor with optional dictionary
    bool initialize(const uint8_t* dictionary = nullptr, size_t dictSize = 0);

    // Decompress a single frame
    GDeflateResult decompress(
        const uint8_t* compressedData,
        size_t compressedSize,
        uint8_t* outputBuffer,
        size_t outputBufferSize
    );

    // Decompress multi-frame stream
    GDeflateResult decompressStream(
        const uint8_t* compressedData,
        size_t compressedSize,
        std::vector<uint8_t>& output
    );

    // Query uncompressed size from header without decompressing
    static size_t queryUncompressedSize(const uint8_t* data, size_t dataSize);

    // Check if data is GDeflate compressed
    static bool isGDeflateCompressed(const uint8_t* data, size_t dataSize);

    // Get last error message
    const char* getLastError() const { return m_lastError; }

private:
    // Internal decompression state
    struct State;
    State* m_state;
    const char* m_lastError;

    // Huffman table decoding
    bool decodeHuffmanTable(const uint8_t*& src, const uint8_t* srcEnd);

    // Literal/length/distance decoding
    int decodeLiteralOrLength();
    int decodeDistance(int lengthCode);

    // Bit reader helpers
    uint32_t readBits(int count);
    void skipBits(int count);
    bool ensureBits(int count);
};

// Convenience function for one-shot decompression
GDeflateResult gdeflateDecompress(
    const uint8_t* compressedData,
    size_t compressedSize,
    std::vector<uint8_t>& output
);

} // namespace compression
} // namespace dedsec