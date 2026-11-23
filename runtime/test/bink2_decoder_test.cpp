// DedSec Fork — Bink 2 Video Decoder Unit Tests (Sprint 4)
// Tests container parsing, synthetic frame generation, audio extraction,
// and decode statistics. Uses in-memory buffers — no game data required.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../src/bink2_decoder.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace dedsec::media;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_testsFailed++; \
        return; \
    } \
} while(0)

#define PASS() do { g_testsPassed++; } while(0)

// ─── Helper: Build a minimal valid Bink2 container in memory ──────────────────

struct Bink2TestBuilder {
    std::vector<uint8_t> data;

    void writeU32(uint32_t val) {
        data.push_back(static_cast<uint8_t>(val & 0xFF));
        data.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        data.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    void writeU64(uint64_t val) {
        for (int i = 0; i < 8; i++) {
            data.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
        }
    }

    // Build a minimal BIK2 file with given parameters
    std::vector<uint8_t> build(uint32_t width, uint32_t height,
                                uint32_t frameCount, uint32_t numAudioTracks) {
        data.clear();

        // Signature "BIK2"
        data.push_back('B'); data.push_back('I');
        data.push_back('K'); data.push_back('2');

        // fileSize (placeholder, will be updated)
        size_t fileSizePos = data.size();
        writeU32(0);

        // frameCount
        writeU32(frameCount);
        // width
        writeU32(width);
        // height
        writeU32(height);
        // fpsNum (30)
        writeU32(30);
        // fpsDen (1)
        writeU32(1);
        // numAudioTracks
        writeU32(numAudioTracks);
        // flags
        writeU32(0);
        // maxWidth
        writeU32(width);
        // maxHeight
        writeU32(height);

        // Audio track descriptors (20 bytes each)
        for (uint32_t i = 0; i < numAudioTracks; i++) {
            writeU32(48000);  // sampleRate
            writeU32(2);      // channels
            writeU32(0x0001); // codecId (PCM)
            writeU32(frameCount * 1600); // totalSamples (~1600/frame at 30fps)
            writeU32(16);     // bitsPerSample
        }

        // Frame index entries (16 bytes each)
        uint64_t frameDataOffset = data.size() + frameCount * 16;
        for (uint32_t i = 0; i < frameCount; i++) {
            writeU64(frameDataOffset + i * 64); // offset
            writeU32(64);                        // size
            writeU32(i == 0 ? 1 : 0);           // flags (keyframe for first)
        }

        // Dummy frame data (64 bytes per frame)
        for (uint32_t i = 0; i < frameCount; i++) {
            for (int j = 0; j < 64; j++) {
                data.push_back(static_cast<uint8_t>(i ^ j));
            }
        }

        // Update file size
        uint32_t totalSize = static_cast<uint32_t>(data.size());
        data[fileSizePos + 0] = static_cast<uint8_t>(totalSize & 0xFF);
        data[fileSizePos + 1] = static_cast<uint8_t>((totalSize >> 8) & 0xFF);
        data[fileSizePos + 2] = static_cast<uint8_t>((totalSize >> 16) & 0xFF);
        data[fileSizePos + 3] = static_cast<uint8_t>((totalSize >> 24) & 0xFF);

        return data;
    }
};

// ─── Signature Validation Tests ───────────────────────────────────────────────

static void test_is_bink2_valid_signatures() {
    uint8_t bik2[] = {'B', 'I', 'K', '2'};
    CHECK(Bink2Decoder::isBink2File(bik2, 4));

    uint8_t kb2a[] = {'K', 'B', '2', 'A'};
    CHECK(Bink2Decoder::isBink2File(kb2a, 4));

    PASS();
}

static void test_is_bink2_invalid_signatures() {
    uint8_t bad1[] = {'X', 'I', 'K', '2'};
    CHECK(!Bink2Decoder::isBink2File(bad1, 4));

    uint8_t bad2[] = {'B', 'I', 'K', '1'};
    CHECK(!Bink2Decoder::isBink2File(bad2, 4));

    CHECK(!Bink2Decoder::isBink2File(nullptr, 4));
    CHECK(!Bink2Decoder::isBink2File(bad1, 0));
    CHECK(!Bink2Decoder::isBink2File(bad1, 3));

    PASS();
}

// ─── Container Parsing Tests ──────────────────────────────────────────────────

static void test_open_from_memory_basic() {
    Bink2TestBuilder builder;
    auto data = builder.build(1920, 1080, 10, 1);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    const auto& header = decoder.getHeader();
    CHECK(header.width == 1920);
    CHECK(header.height == 1080);
    CHECK(header.frameCount == 10);
    CHECK(header.fpsNum == 30);
    CHECK(header.fpsDen == 1);
    CHECK(header.numAudioTracks == 1);

    decoder.close();
    PASS();
}

static void test_audio_track_parsing() {
    Bink2TestBuilder builder;
    auto data = builder.build(1280, 720, 5, 2);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    CHECK(decoder.getAudioTrackCount() == 2);

    const auto& track0 = decoder.getAudioTrack(0);
    CHECK(track0.sampleRate == 48000);
    CHECK(track0.channels == 2);
    CHECK(track0.bitsPerSample == 16);

    const auto& track1 = decoder.getAudioTrack(1);
    CHECK(track1.sampleRate == 48000);

    // Out-of-bounds should return empty track
    const auto& trackBad = decoder.getAudioTrack(99);
    CHECK(trackBad.sampleRate == 0);

    decoder.close();
    PASS();
}

static void test_frame_index_parsing() {
    Bink2TestBuilder builder;
    auto data = builder.build(640, 480, 8, 0);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    // First frame should be marked as keyframe
    const auto& entry0 = decoder.getFrameEntry(0);
    CHECK(entry0.size == 64);
    CHECK((entry0.flags & 1) == 1); // keyframe flag

    // Subsequent frames should not be keyframes
    const auto& entry1 = decoder.getFrameEntry(1);
    CHECK((entry1.flags & 1) == 0);

    // Out-of-bounds returns empty entry
    const auto& entryBad = decoder.getFrameEntry(999);
    CHECK(entryBad.size == 0);

    decoder.close();
    PASS();
}

static void test_open_invalid_data() {
    Bink2Decoder decoder;

    // Null data
    CHECK(!decoder.openFromMemory(nullptr, 0));

    // Too small
    uint8_t tiny[] = {'B', 'I'};
    CHECK(!decoder.openFromMemory(tiny, sizeof(tiny)));

    // Wrong signature
    uint8_t bad[] = {'X', 'Y', 'Z', 'W', 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!decoder.openFromMemory(bad, sizeof(bad)));

    PASS();
}

// ─── Video Decode Tests ───────────────────────────────────────────────────────

static void test_decode_frame_dimensions() {
    Bink2TestBuilder builder;
    auto data = builder.build(320, 240, 5, 0);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    Bink2VideoFrame frame{};
    CHECK(decoder.decodeFrame(0, frame));
    CHECK(frame.width == 320);
    CHECK(frame.height == 240);
    CHECK(frame.yPlane != nullptr);
    CHECK(frame.uPlane != nullptr);
    CHECK(frame.vPlane != nullptr);
    CHECK(frame.yStride == 320);
    CHECK(frame.uvStride == 160); // (320+1)/2

    decoder.close();
    PASS();
}

static void test_decode_sequential_frames() {
    Bink2TestBuilder builder;
    auto data = builder.build(160, 120, 4, 0);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    // Sequential decode
    for (uint32_t i = 0; i < 4; i++) {
        Bink2VideoFrame frame{};
        CHECK(decoder.decodeNextFrame(frame));
        CHECK(frame.width == 160);
        CHECK(frame.height == 120);
    }

    // Past end should fail
    Bink2VideoFrame extra{};
    CHECK(!decoder.decodeNextFrame(extra));

    decoder.close();
    PASS();
}

static void test_decode_out_of_bounds() {
    Bink2TestBuilder builder;
    auto data = builder.build(64, 64, 3, 0);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    Bink2VideoFrame frame{};
    CHECK(!decoder.decodeFrame(999, frame));

    decoder.close();
    PASS();
}

static void test_keyframe_flag() {
    Bink2TestBuilder builder;
    auto data = builder.build(64, 64, 3, 0);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    Bink2VideoFrame frame0{};
    CHECK(decoder.decodeFrame(0, frame0));
    CHECK(frame0.isKeyframe == true);

    Bink2VideoFrame frame1{};
    CHECK(decoder.decodeFrame(1, frame1));
    CHECK(frame1.isKeyframe == false);

    decoder.close();
    PASS();
}

static void test_presentation_timestamps() {
    Bink2TestBuilder builder;
    auto data = builder.build(64, 64, 10, 0);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    Bink2VideoFrame f0{}, f1{};
    decoder.decodeFrame(0, f0);
    decoder.decodeFrame(1, f1);

    // PTS should increase monotonically
    CHECK(f1.presentationTimeUs > f0.presentationTimeUs);

    // At 30fps, frame 1 should be ~33333us after frame 0
    double diffUs = static_cast<double>(f1.presentationTimeUs - f0.presentationTimeUs);
    CHECK(diffUs > 30000.0 && diffUs < 40000.0);

    decoder.close();
    PASS();
}

// ─── Audio Extraction Tests ───────────────────────────────────────────────────

static void test_extract_audio_basic() {
    Bink2TestBuilder builder;
    auto data = builder.build(320, 240, 5, 1);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    uint8_t audioBuf[8192];
    uint32_t bytesWritten = 0;
    CHECK(decoder.extractAudioData(0, 0, audioBuf, sizeof(audioBuf), bytesWritten));
    CHECK(bytesWritten > 0);

    // Should be silence (all zeros) in stub implementation
    bool allZero = true;
    for (uint32_t i = 0; i < bytesWritten && i < 64; i++) {
        if (audioBuf[i] != 0) { allZero = false; break; }
    }
    CHECK(allZero);

    decoder.close();
    PASS();
}

static void test_extract_audio_invalid_track() {
    Bink2TestBuilder builder;
    auto data = builder.build(64, 64, 3, 1);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    uint8_t buf[256];
    uint32_t written = 0;
    CHECK(!decoder.extractAudioData(99, 0, buf, sizeof(buf), written));
    CHECK(written == 0);

    decoder.close();
    PASS();
}

// ─── Statistics Tests ─────────────────────────────────────────────────────────

static void test_decode_statistics() {
    Bink2TestBuilder builder;
    auto data = builder.build(64, 64, 5, 0);

    Bink2Decoder decoder;
    CHECK(decoder.openFromMemory(data.data(), data.size()));

    // Initial stats should be zero
    auto stats0 = decoder.getStats();
    CHECK(stats0.framesDecoded == 0);
    CHECK(stats0.framesDropped == 0);

    // Decode 3 frames
    for (uint32_t i = 0; i < 3; i++) {
        Bink2VideoFrame frame{};
        decoder.decodeFrame(i, frame);
    }

    auto stats1 = decoder.getStats();
    CHECK(stats1.framesDecoded == 3);
    CHECK(stats1.decodeTimeAvgMs >= 0.0);
    CHECK(stats1.decodeTimeMaxMs >= 0.0);
    CHECK(stats1.bytesRead > 0);

    decoder.close();
    PASS();
}

// ─── Close / Reopen Tests ─────────────────────────────────────────────────────

static void test_close_and_reopen() {
    Bink2TestBuilder builder;
    auto data1 = builder.build(64, 64, 3, 0);
    auto data2 = builder.build(128, 96, 7, 1);

    Bink2Decoder decoder;

    // Open first
    CHECK(decoder.openFromMemory(data1.data(), data1.size()));
    CHECK(decoder.getHeader().width == 64);
    decoder.close();

    // Reopen with different data
    CHECK(decoder.openFromMemory(data2.data(), data2.size()));
    CHECK(decoder.getHeader().width == 128);
    CHECK(decoder.getHeader().height == 96);
    CHECK(decoder.getHeader().frameCount == 7);
    CHECK(decoder.getAudioTrackCount() == 1);

    decoder.close();
    PASS();
}

static void test_operations_on_closed_decoder() {
    Bink2Decoder decoder;

    Bink2VideoFrame frame{};
    CHECK(!decoder.decodeFrame(0, frame));
    CHECK(!decoder.decodeNextFrame(frame));

    uint8_t buf[64];
    uint32_t written = 0;
    CHECK(!decoder.extractAudioData(0, 0, buf, sizeof(buf), written));

    // Header should be zeroed
    CHECK(decoder.getHeader().width == 0);
    CHECK(decoder.getHeader().frameCount == 0);

    PASS();
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== DedSec Bink2 Decoder Unit Tests ===\n");

    test_is_bink2_valid_signatures();
    test_is_bink2_invalid_signatures();
    test_open_from_memory_basic();
    test_audio_track_parsing();
    test_frame_index_parsing();
    test_open_invalid_data();
    test_decode_frame_dimensions();
    test_decode_sequential_frames();
    test_decode_out_of_bounds();
    test_keyframe_flag();
    test_presentation_timestamps();
    test_extract_audio_basic();
    test_extract_audio_invalid_track();
    test_decode_statistics();
    test_close_and_reopen();
    test_operations_on_closed_decoder();

    std::printf("Passed: %d | Failed: %d\n", g_testsPassed, g_testsFailed);
    return g_testsFailed > 0 ? 1 : 0;
}