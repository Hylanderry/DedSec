// DedSec Fork — GDeflate Decompressor Implementation
// Xbox Series asset compression format (Microsoft proprietary)
// Clean-room implementation based on public documentation and behavior analysis.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "gdeflate_decompressor.h"
#include <cstring>
#include <algorithm>

namespace dedsec {
namespace compression {

// GDeflate magic bytes
static constexpr uint32_t GDEFLATE_MAGIC = 0x4445464C; // "DEFL"
static constexpr uint32_t GDEFLATE_MAGIC_ALT = 0x47444546; // "GDEF"

// Huffman table constants
static constexpr int MAX_BITS = 15;
static constexpr int MAX_LITERALS = 286;
static constexpr int MAX_DISTANCES = 30;
static constexpr int MAX_CODE_LENGTHS = 19;

// Length base values for codes 257-285
static const uint16_t LENGTH_BASE[] = {
 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

// Extra bits for length codes
static const uint8_t LENGTH_EXTRA[] = {
 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

// Distance base values
static const uint16_t DISTANCE_BASE[] = {
 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

// Extra bits for distance codes
static const uint8_t DISTANCE_EXTRA[] = {
 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

// Code length alphabet order
static const uint8_t CODE_LENGTH_ORDER[] = {
 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

struct GDeflateDecompressor::State {
 // Bit reader state
 const uint8_t* bitBuffer;
 size_t bitBufferSize;
 size_t bitPos;

 // Huffman tables
 uint16_t literalTable[1 << MAX_BITS];
 uint16_t distanceTable[1 << MAX_BITS];
 uint8_t literalLengths[MAX_LITERALS];
 uint8_t distanceLengths[MAX_DISTANCES];

 // Output window (32KB sliding window)
 uint8_t window[32768];
 size_t windowPos;
 size_t windowSize;

 bool initialized;
};

GDeflateDecompressor::GDeflateDecompressor()
 : m_state(new State()), m_lastError(nullptr) {
 std::memset(m_state, 0, sizeof(State));
}

GDeflateDecompressor::~GDeflateDecompressor() {
 delete m_state;
}

bool GDeflateDecompressor::initialize(const uint8_t* dictionary, size_t dictSize) {
 if (!m_state) return false;

 m_state->bitBuffer = nullptr;
 m_state->bitBufferSize = 0;
 m_state->bitPos = 0;
 m_state->windowPos = 0;
 m_state->windowSize = 0;
 m_state->initialized = true;

 // Load dictionary if provided
 if (dictionary && dictSize > 0) {
 size_t copySize = std::min(dictSize, sizeof(m_state->window));
 std::memcpy(m_state->window, dictionary, copySize);
 m_state->windowSize = copySize;
 m_state->windowPos = copySize & 0x7FFF;
 }

 return true;
}

uint32_t GDeflateDecompressor::readBits(int count) {
 if (count <= 0 || count > 32) return 0;

 uint32_t value = 0;
 for (int i = 0; i < count; i++) {
 size_t byteIdx = m_state->bitPos >> 3;
 size_t bitIdx = m_state->bitPos & 7;

 if (byteIdx >= m_state->bitBufferSize) {
 m_lastError = "Bit buffer underflow";
 return 0;
 }

 if (m_state->bitBuffer[byteIdx] & (1 << bitIdx)) {
 value |= (1u << i);
 }
 m_state->bitPos++;
 }

 return value;
}

void GDeflateDecompressor::skipBits(int count) {
 m_state->bitPos += count;
}

bool GDeflateDecompressor::ensureBits(int count) {
 size_t bytesNeeded = (m_state->bitPos + count + 7) >> 3;
 return bytesNeeded <= m_state->bitBufferSize;
}

bool GDeflateDecompressor::decodeHuffmanTable(const uint8_t*& src, const uint8_t* srcEnd) {
 // Simplified: use fixed Huffman tables for now
 // Full dynamic table decoding would go here

 // Fixed literal/length table
 for (int i = 0; i <= 143; i++) m_state->literalLengths[i] = 8;
 for (int i = 144; i <= 255; i++) m_state->literalLengths[i] = 9;
 for (int i = 256; i <= 279; i++) m_state->literalLengths[i] = 7;
 for (int i = 280; i <= 287; i++) m_state->literalLengths[i] = 8;

 // Fixed distance table
 for (int i = 0; i < 30; i++) m_state->distanceLengths[i] = 5;

 return true;
}

int GDeflateDecompressor::decodeLiteralOrLength() {
 // Simplified fixed-table decode
 uint32_t code = readBits(7);
 if (code < 48) {
 skipBits(1); // 7-bit codes: 256-279
 return 256 + code;
 } else if (code < 192) {
 skipBits(2); // 8-bit codes: 0-143
 return code - 48;
 } else if (code < 200) {
 skipBits(2); // 8-bit codes: 280-287
 return 280 + (code - 192);
 } else {
 skipBits(1); // 9-bit codes: 144-255
 return 144 + (code - 192);
 }
}

int GDeflateDecompressor::decodeDistance(int lengthCode) {
 uint32_t dist = readBits(5);
 if (dist < 30) {
 int extra = DISTANCE_EXTRA[dist];
 if (extra > 0) {
 dist = DISTANCE_BASE[dist] + readBits(extra);
 } else {
 dist = DISTANCE_BASE[dist];
 }
 return static_cast<int>(dist);
 }
 return -1;
}

GDeflateResult GDeflateDecompressor::decompress(
 const uint8_t* compressedData,
 size_t compressedSize,
 uint8_t* outputBuffer,
 size_t outputBufferSize) {

 GDeflateResult result = {false, 0, nullptr};

 if (!m_state || !m_state->initialized) {
 result.errorMessage = "Decompressor not initialized";
 return result;
 }

 if (!compressedData || compressedSize == 0) {
 result.errorMessage = "Invalid input data";
 return result;
 }

 if (!outputBuffer || outputBufferSize == 0) {
 result.errorMessage = "Invalid output buffer";
 return result;
 }

 // Check for GDeflate header
 if (compressedSize < sizeof(GDeflateFrameHeader)) {
 result.errorMessage = "Data too small for header";
 return result;
 }

 const auto* header = reinterpret_cast<const GDeflateFrameHeader*>(compressedData);

 // Validate magic
 if (header->magic != GDEFLATE_MAGIC && header->magic != GDEFLATE_MAGIC_ALT) {
 // Try raw deflate without header
 m_state->bitBuffer = compressedData;
 m_state->bitBufferSize = compressedSize;
 } else {
 // Skip header
 m_state->bitBuffer = compressedData + sizeof(GDeflateFrameHeader);
 m_state->bitBufferSize = compressedSize - sizeof(GDeflateFrameHeader);
 }

 m_state->bitPos = 0;

 // Decompress using DEFLATE algorithm
 size_t outputPos = 0;
 bool lastBlock = false;

 while (!lastBlock && outputPos < outputBufferSize) {
 if (!ensureBits(3)) break;

 lastBlock = readBits(1) != 0;
 uint32_t blockType = readBits(2);

 if (blockType == 0) {
 // Stored (uncompressed) block
 skipBits(static_cast<int>(m_state->bitPos % 8)); // Align to byte boundary
 uint16_t len = static_cast<uint16_t>(readBits(16));
 uint16_t nlen = static_cast<uint16_t>(readBits(16));

 if (len != (~nlen & 0xFFFF)) {
 result.errorMessage = "Invalid stored block length";
 return result;
 }

 if (outputPos + len > outputBufferSize) {
 result.errorMessage = "Output buffer overflow";
 return result;
 }

 size_t bytePos = m_state->bitPos >> 3;
 if (bytePos + len > m_state->bitBufferSize) {
 result.errorMessage = "Input buffer underflow";
 return result;
 }

 std::memcpy(outputBuffer + outputPos, m_state->bitBuffer + bytePos, len);
 outputPos += len;
 m_state->bitPos = (bytePos + len) << 3;

 } else if (blockType == 1 || blockType == 2) {
 // Compressed block (fixed or dynamic Huffman)
 if (blockType == 2) {
 const uint8_t* dummy = nullptr;
 if (!decodeHuffmanTable(dummy, nullptr)) {
 result.errorMessage = "Failed to decode Huffman table";
 return result;
 }
 }

 while (outputPos < outputBufferSize) {
 int symbol = decodeLiteralOrLength();

 if (symbol < 0) {
 result.errorMessage = "Decode error";
 return result;
 }

 if (symbol == 256) {
 // End of block
 break;
 } else if (symbol < 256) {
 // Literal byte
 outputBuffer[outputPos++] = static_cast<uint8_t>(symbol);
 m_state->window[m_state->windowPos] = static_cast<uint8_t>(symbol);
 m_state->windowPos = (m_state->windowPos + 1) & 0x7FFF;
 } else {
 // Length/distance pair
 int lengthIdx = symbol - 257;
 if (lengthIdx < 0 || lengthIdx > 28) {
 result.errorMessage = "Invalid length code";
 return result;
 }

 int length = LENGTH_BASE[lengthIdx];
 int extraBits = LENGTH_EXTRA[lengthIdx];
 if (extraBits > 0) {
 length += readBits(extraBits);
 }

 int distance = decodeDistance(lengthIdx);
 if (distance < 0) {
 result.errorMessage = "Invalid distance code";
 return result;
 }

 // Copy from window
 size_t srcPos = (m_state->windowPos - distance) & 0x7FFF;
 for (int i = 0; i < length && outputPos < outputBufferSize; i++) {
 uint8_t byte = m_state->window[srcPos];
 outputBuffer[outputPos++] = byte;
 m_state->window[m_state->windowPos] = byte;
 m_state->windowPos = (m_state->windowPos + 1) & 0x7FFF;
 srcPos = (srcPos + 1) & 0x7FFF;
 }
 }
 }
 } else {
 result.errorMessage = "Invalid block type";
 return result;
 }
 }

 result.success = true;
 result.bytesDecompressed = outputPos;
 return result;
}

GDeflateResult GDeflateDecompressor::decompressStream(
 const uint8_t* compressedData,
 size_t compressedSize,
 std::vector<uint8_t>& output) {

 // Query uncompressed size first
 size_t expectedSize = queryUncompressedSize(compressedData, compressedSize);
 if (expectedSize > 0) {
 output.resize(expectedSize);
 } else {
 output.resize(compressedSize * 4); // Estimate 4x expansion
 }

 GDeflateResult result = decompress(compressedData, compressedSize,
 output.data(), output.size());

 if (result.success) {
 output.resize(result.bytesDecompressed);
 }

 return result;
}

size_t GDeflateDecompressor::queryUncompressedSize(const uint8_t* data, size_t dataSize) {
 if (dataSize < sizeof(GDeflateFrameHeader)) return 0;

 const auto* header = reinterpret_cast<const GDeflateFrameHeader*>(data);
 if (header->magic == GDEFLATE_MAGIC || header->magic == GDEFLATE_MAGIC_ALT) {
 return header->uncompressedSize;
 }

 return 0;
}

bool GDeflateDecompressor::isGDeflateCompressed(const uint8_t* data, size_t dataSize) {
 if (dataSize < sizeof(uint32_t)) return false;

 uint32_t magic = *reinterpret_cast<const uint32_t*>(data);
 return magic == GDEFLATE_MAGIC || magic == GDEFLATE_MAGIC_ALT;
}

GDeflateResult gdeflateDecompress(
 const uint8_t* compressedData,
 size_t compressedSize,
 std::vector<uint8_t>& output) {

 GDeflateDecompressor decompressor;
 if (!decompressor.initialize()) {
 return {false, 0, "Failed to initialize decompressor"};
 }

 return decompressor.decompressStream(compressedData, compressedSize, output);
}

} // namespace compression
} // namespace dedsec