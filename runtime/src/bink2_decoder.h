// DedSec Fork — Bink 2 Video Decoder (Clean-Room Container Parser)
// Parses the Bink 2 (.bik/.bk2) container format: header, frame index table,
// audio track descriptors. Video decode is stubbed with synthetic gradient
// frames; real codec integration will follow in a later sprint.
//
// Design principles:
// - No dependency on bink2.dll / Bink2Scarlett.dll
// - Streaming: only one decoded frame resident at a time
// - Memory-safe: all offsets validated against file size before read
// - VFS-ready: open() uses direct I/O now, swappable to dedsec::vfs later
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <cstddef>

namespace dedsec {
namespace media {

// ============================================================================
// Bink 2 Container Header (parsed from first bytes of file)
// ============================================================================

struct Bink2Header {
    char signature[4];          // "BIK2" or "KB2A"
    uint32_t fileSize;
    uint32_t frameCount;
    uint32_t width;
    uint32_t height;
    uint32_t fpsNum;
    uint32_t fpsDen;
    uint32_t numAudioTracks;
    uint32_t flags;
    uint32_t maxWidth;          // for resolution scaling
    uint32_t maxHeight;
};

// ============================================================================
// Audio Track Info Within Bink Container
// ============================================================================

struct Bink2AudioTrack {
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t codecId;           // Bink audio codec identifier
    uint32_t totalSamples;
    uint32_t bitsPerSample;
};

// ============================================================================
// Frame Index Entry (offset + size in file)
// ============================================================================

struct Bink2FrameEntry {
    uint64_t offset;            // byte offset in file
    uint32_t size;              // compressed frame size
    uint32_t flags;             // keyframe, etc.
};

// ============================================================================
// Decoded Video Frame (YUV Planes)
// ============================================================================

struct Bink2VideoFrame {
    uint32_t width;
    uint32_t height;
    uint8_t* yPlane;            // Y plane (luma)
    uint8_t* uPlane;            // U plane (chroma)
    uint8_t* vPlane;            // V plane (chroma)
    uint32_t yStride;
    uint32_t uvStride;
    uint64_t presentationTimeUs;
    bool isKeyframe;
};

// ============================================================================
// Decode Statistics
// ============================================================================

struct Bink2DecodeStats {
    uint64_t framesDecoded;
    uint64_t framesDropped;
    double decodeTimeAvgMs;
    double decodeTimeMaxMs;
    uint64_t bytesRead;
    uint64_t audioBytesExtracted;
};

// ============================================================================
// Streaming Bink 2 Decoder
// ============================================================================

class Bink2Decoder {
public:
    Bink2Decoder();
    ~Bink2Decoder();

    // Open from file path (uses direct file I/O; VFS integration wired later)
    bool open(const char* filePath);

    // Open from memory buffer (for testing without files)
    bool openFromMemory(const uint8_t* data, size_t size);

    void close();

    // Container info
    const Bink2Header& getHeader() const;
    uint32_t getAudioTrackCount() const;
    const Bink2AudioTrack& getAudioTrack(uint32_t index) const;
    const Bink2FrameEntry& getFrameEntry(uint32_t frameIndex) const;

    // Video decode (streaming: only current frame in memory)
    bool decodeFrame(uint32_t frameIndex, Bink2VideoFrame& outFrame);

    // Sequential decode (next frame)
    bool decodeNextFrame(Bink2VideoFrame& outFrame);

    // Audio extraction for a specific track
    bool extractAudioData(uint32_t trackIndex, uint32_t frameIndex,
                          uint8_t* outBuffer, uint32_t bufferSize,
                          uint32_t& bytesWritten);

    // Get decode statistics
    Bink2DecodeStats getStats() const;

    // Check if header bytes indicate a valid Bink 2 file
    static bool isBink2File(const uint8_t* header, size_t headerSize);

    // Non-copyable
    Bink2Decoder(const Bink2Decoder&) = delete;
    Bink2Decoder& operator=(const Bink2Decoder&) = delete;

private:
    struct Impl;
    Impl* impl_;
};

// ============================================================================
// YUV -> RGB Conversion Compute Shader Descriptor (D3D12 Integration)
// ============================================================================

// The actual HLSL shader is compiled separately; this provides binding info
// and constant buffer layout for the compute shader that converts decoded
// YUV frames to RGB for presentation.

struct YuvToRgbShaderDesc {
    enum class Format : uint8_t { NV12, YV12, YUY2 };
    Format inputFormat;
    uint32_t outputWidth;
    uint32_t outputHeight;

    // CBV layout for the compute shader
    struct Constants {
        uint32_t width;
        uint32_t height;
        uint32_t yStride;
        uint32_t uvStride;
        float colorMatrix[9];   // BT.709 or BT.601 coefficients
    };
};

} // namespace media
} // namespace dedsec