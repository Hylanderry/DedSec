// DedSec Fork — XAudio2 Full Unit Tests (Sprint 4)
// Tests voice pool allocation, playback control, 3D spatial audio math,
// codec stubs, and statistics tracking. No game data required.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../shims/xaudio2_full.h"
#include <cstdio>
#include <cmath>
#include <cstring>

using namespace dedsec::audio;

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

// ─── Initialization Tests ─────────────────────────────────────────────────────

static void test_init_shutdown() {
    XAudio2Full engine;
    CHECK(!engine.isInitialized());
    CHECK(engine.initialize(48000, 2));
    CHECK(engine.isInitialized());
    CHECK(engine.getMasterVoice() != kInvalidVoice);
    CHECK(engine.getActiveVoiceCount() == 1); // Master voice only
    engine.shutdown();
    CHECK(!engine.isInitialized());
    PASS();
}

static void test_double_init_safe() {
    XAudio2Full engine;
    CHECK(engine.initialize());
    CHECK(engine.initialize()); // Should be safe no-op
    CHECK(engine.isInitialized());
    engine.shutdown();
    PASS();
}

// ─── Voice Pool Tests ─────────────────────────────────────────────────────────

static void test_create_destroy_source_voice() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.type = VoiceType::Source;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 2;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);
    CHECK(h != kInvalidVoice);
    CHECK(engine.getActiveVoiceCount() == 2); // Master + source

    engine.destroyVoice(h);
    CHECK(engine.getActiveVoiceCount() == 1); // Master only

    engine.shutdown();
    PASS();
}

static void test_voice_pool_exhaustion() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 1;
    params.format.samplesPerSec = 44100;
    params.format.bitsPerSample = 16;

    VoiceHandle handles[kMaxVoices];
    uint32_t created = 0;

    // Allocate all slots (minus master which is slot 0)
    for (uint32_t i = 0; i < kMaxVoices; i++) {
        handles[i] = engine.createSourceVoice(params);
        if (handles[i] != kInvalidVoice) created++;
    }

    // Should have created kMaxVoices - 1 (master occupies one slot)
    CHECK(created == kMaxVoices - 1);

    // Next allocation should fail
    VoiceHandle overflow = engine.createSourceVoice(params);
    CHECK(overflow == kInvalidVoice);

    // Cleanup
    for (uint32_t i = 0; i < created; i++) {
        engine.destroyVoice(handles[i]);
    }

    engine.shutdown();
    PASS();
}

static void test_submix_voice_creation() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.type = VoiceType::Submix;
    params.format.channels = 2;
    params.format.samplesPerSec = 48000;

    VoiceHandle h = engine.createSubmixVoice(params);
    CHECK(h != kInvalidVoice);
    CHECK(engine.getActiveVoiceCount() == 2);

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

static void test_cannot_destroy_master() {
    XAudio2Full engine;
    engine.initialize();

    VoiceHandle master = engine.getMasterVoice();
    engine.destroyVoice(master); // Should be a no-op
    CHECK(engine.getActiveVoiceCount() == 1); // Master still alive

    engine.shutdown();
    PASS();
}

// ─── Playback Control Tests ───────────────────────────────────────────────────

static void test_volume_control() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 2;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);
    CHECK(h != kInvalidVoice);

    // Default volume should be 1.0
    CHECK(std::abs(engine.getVolume(h) - 1.0f) < 0.001f);

    engine.setVolume(h, 0.5f);
    CHECK(std::abs(engine.getVolume(h) - 0.5f) < 0.001f);

    // Clamp test: > 2.0 should clamp to 2.0
    engine.setVolume(h, 5.0f);
    CHECK(std::abs(engine.getVolume(h) - 2.0f) < 0.001f);

    // Clamp test: < 0.0 should clamp to 0.0
    engine.setVolume(h, -1.0f);
    CHECK(std::abs(engine.getVolume(h) - 0.0f) < 0.001f);

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

static void test_frequency_ratio() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 1;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;
    params.maxFrequencyRatio = 2.0f;

    VoiceHandle h = engine.createSourceVoice(params);

    engine.setFrequencyRatio(h, 1.5f);
    CHECK(std::abs(engine.getFrequencyRatio(h) - 1.5f) < 0.001f);

    // Should clamp to maxFrequencyRatio
    engine.setFrequencyRatio(h, 3.0f);
    CHECK(std::abs(engine.getFrequencyRatio(h) - 2.0f) < 0.001f);

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

static void test_start_stop_pause_resume() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 1;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);
    CHECK(engine.startVoice(h));
    CHECK(engine.pauseVoice(h));
    CHECK(engine.resumeVoice(h));
    CHECK(engine.stopVoice(h));

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

// ─── Buffer Submission Tests ──────────────────────────────────────────────────

static void test_submit_buffer_pcm() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 1;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);

    uint8_t testData[1024] = {};
    AudioBuffer buf{};
    buf.pAudioData = testData;
    buf.audioBytes = sizeof(testData);

    CHECK(engine.submitBuffer(h, buf));

    auto stats = engine.getStats();
    CHECK(stats.buffersSubmitted >= 1);

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

static void test_flush_buffers() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 1;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);

    uint8_t testData[256] = {};
    AudioBuffer buf{};
    buf.pAudioData = testData;
    buf.audioBytes = sizeof(testData);

    engine.submitBuffer(h, buf);
    CHECK(engine.flushBuffers(h));

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

// ─── 3D Spatial Audio Tests ───────────────────────────────────────────────────

static void test_3d_distance_attenuation() {
    XAudio2Full engine;
    engine.initialize();

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 2;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);

    AudioListener listener;
    listener.position = {0, 0, 0};
    listener.forward = {0, 0, 1};
    listener.up = {0, 1, 0};
    engine.setListener(listener);

    // Emitter at origin → full volume
    AudioEmitter emitter;
    emitter.position = {0, 0, 0};
    emitter.innerRadius = 1.0f;
    emitter.outerRadius = 100.0f;
    engine.setEmitter(h, emitter);
    engine.apply3DAudio(h);
    float volAtOrigin = engine.getVolume(h);
    CHECK(volAtOrigin > 0.9f);

    // Emitter far away → low volume
    emitter.position = {50, 0, 0};
    engine.setEmitter(h, emitter);
    engine.apply3DAudio(h);
    float volFar = engine.getVolume(h);
    CHECK(volFar < volAtOrigin);

    // Emitter beyond outer radius → zero volume
    emitter.position = {200, 0, 0};
    engine.setEmitter(h, emitter);
    engine.apply3DAudio(h);
    float volBeyond = engine.getVolume(h);
    CHECK(volBeyond < 0.01f);

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

static void test_doppler_factor() {
    XAudio2Full engine;
    engine.initialize();

    engine.setDopplerFactor(0.0f); // Disable doppler
    // With factor 0, frequency ratio should remain ~1.0 regardless of velocity

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 1;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);

    AudioListener listener;
    listener.position = {0, 0, 0};
    listener.velocity = {0, 0, 0};
    listener.forward = {0, 0, 1};
    engine.setListener(listener);

    AudioEmitter emitter;
    emitter.position = {10, 0, 0};
    emitter.velocity = {-100, 0, 0}; // Moving toward listener fast
    engine.setEmitter(h, emitter);
    engine.apply3DAudio(h);

    float ratio = engine.getFrequencyRatio(h);
    // With doppler factor 0, ratio should be ~1.0
    CHECK(std::abs(ratio - 1.0f) < 0.01f);

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

// ─── Statistics Tests ─────────────────────────────────────────────────────────

static void test_decode_stats() {
    XAudio2Full engine;
    engine.initialize();

    auto statsBefore = engine.getStats();
    CHECK(statsBefore.poolCapacity == kMaxVoices);

    VoiceParams params;
    params.format.formatTag = static_cast<uint16_t>(AudioCodec::PCM);
    params.format.channels = 1;
    params.format.samplesPerSec = 48000;
    params.format.bitsPerSample = 16;

    VoiceHandle h = engine.createSourceVoice(params);

    uint8_t data[512] = {};
    AudioBuffer buf{};
    buf.pAudioData = data;
    buf.audioBytes = sizeof(data);

    engine.submitBuffer(h, buf);

    auto statsAfter = engine.getStats();
    CHECK(statsAfter.buffersSubmitted > statsBefore.buffersSubmitted);
    CHECK(statsAfter.activeVoices == 2); // Master + source

    engine.destroyVoice(h);
    engine.shutdown();
    PASS();
}

// ─── Invalid Handle Safety ────────────────────────────────────────────────────

static void test_invalid_handle_safety() {
    XAudio2Full engine;
    engine.initialize();

    // All operations on invalid handle should be safe no-ops
    CHECK(!engine.startVoice(kInvalidVoice));
    CHECK(!engine.stopVoice(kInvalidVoice));
    CHECK(!engine.pauseVoice(kInvalidVoice));
    CHECK(!engine.resumeVoice(kInvalidVoice));
    CHECK(!engine.flushBuffers(kInvalidVoice));
    CHECK(std::abs(engine.getVolume(kInvalidVoice)) < 0.001f);

    AudioBuffer buf{};
    CHECK(!engine.submitBuffer(kInvalidVoice, buf));

    engine.shutdown();
    PASS();
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== DedSec XAudio2 Full Unit Tests ===\n");

    test_init_shutdown();
    test_double_init_safe();
    test_create_destroy_source_voice();
    test_voice_pool_exhaustion();
    test_submix_voice_creation();
    test_cannot_destroy_master();
    test_volume_control();
    test_frequency_ratio();
    test_start_stop_pause_resume();
    test_submit_buffer_pcm();
    test_flush_buffers();
    test_3d_distance_attenuation();
    test_doppler_factor();
    test_decode_stats();
    test_invalid_handle_safety();

    std::printf("Passed: %d | Failed: %d\n", g_testsPassed, g_testsFailed);
    return g_testsFailed > 0 ? 1 : 0;
}