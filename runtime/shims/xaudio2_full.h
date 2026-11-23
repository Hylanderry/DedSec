// DedSec Fork — XAudio2 Full Implementation (Sprint 4)
// Complete audio pipeline: voice management, 3D spatial audio, decoding stubs,
// WASAPI output, voice pooling, sample rate conversion.
// Clean-room implementation — no dependency on Xbox XAudio2 binaries.
//
// Architecture:
//   Game → IXAudio2Full → VoicePool → SourceVoice/SubmixVoice/MasterVoice
//                                     ↓
//                              AudioDecoder (ADPCM/XMA2/Opus stubs)
//                                     ↓
//                              Spatializer (Doppler + attenuation)
//                                     ↓
//                              WASAPI Output / XAudio2 fallback
//
// Max 64 simultaneous voices. Lock-free voice pool with atomic slot allocation.
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
#include <atomic>
#include <array>
#include <functional>

namespace dedsec {
namespace audio {

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxVoices = 64;
static constexpr uint32_t kMaxSubmixVoices = 8;
static constexpr uint32_t kMaxChannels = 8;
static constexpr uint32_t kDefaultSampleRate = 48000;
static constexpr float kDefaultDopplerFactor = 1.0f;
static constexpr float kSpeedOfSound = 343.5f; // m/s at 20°C

// ─── Wave Format ──────────────────────────────────────────────────────────────

struct WaveFormatEx {
    uint16_t formatTag;       // WAVE_FORMAT_PCM, ADPCM, XMA2, etc.
    uint16_t channels;
    uint32_t samplesPerSec;
    uint32_t avgBytesPerSec;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint16_t extraDataSize;
};

// ─── Audio Codec Identifiers ──────────────────────────────────────────────────

enum class AudioCodec : uint16_t {
    PCM     = 0x0001,
    ADPCM   = 0x0002,
    XMA2    = 0x0166,
    Opus    = 0x00FF,  // Custom tag for our stub
    Unknown = 0xFFFF
};

// ─── Voice Types ──────────────────────────────────────────────────────────────

enum class VoiceType : uint8_t {
    Source,
    Submix,
    Master
};

// ─── Voice State ──────────────────────────────────────────────────────────────

enum class VoiceState : uint8_t {
    Idle,
    Playing,
    Paused,
    Stopped
};

// ─── 3D Emitter / Listener Positions ──────────────────────────────────────────

struct AudioVector3 {
    float x, y, z;
};

struct AudioEmitter {
    AudioVector3 position = {0, 0, 0};
    AudioVector3 velocity = {0, 0, 0};
    float innerRadius = 1.0f;
    float outerRadius = 100.0f;
    float curveMin = 0.0f;
    float curveMax = 1.0f;
};

struct AudioListener {
    AudioVector3 position = {0, 0, 0};
    AudioVector3 velocity = {0, 0, 0};
    AudioVector3 forward  = {0, 0, 1};
    AudioVector3 up       = {0, 1, 0};
};

// ─── Audio Buffer ─────────────────────────────────────────────────────────────

struct AudioBuffer {
    const uint8_t* pAudioData;
    uint32_t audioBytes;
    uint32_t flags;           // XAUDIO2_END_OF_STREAM, LOOP, etc.
    uint32_t playBegin;
    uint32_t playLength;
    uint32_t loopBegin;
    uint32_t loopLength;
    uint32_t loopCount;
    void*    pContext;        // User callback context
};

// ─── Voice Callback Interface ─────────────────────────────────────────────────

class IVoiceCallback {
public:
    virtual ~IVoiceCallback() = default;
    virtual void onBufferEnd(void* pBufferContext) = 0;
    virtual void onStreamEnd() = 0;
    virtual void onError(uint32_t errorCode) = 0;
};

// ─── Voice Handle (opaque index into the pool) ────────────────────────────────

using VoiceHandle = uint32_t;
static constexpr VoiceHandle kInvalidVoice = UINT32_MAX;

// ─── Voice Parameters ─────────────────────────────────────────────────────────

struct VoiceParams {
    VoiceType type = VoiceType::Source;
    WaveFormatEx format = {};
    float maxFrequencyRatio = 2.0f;
    uint32_t processingStage = 0;   // For submix routing
    IVoiceCallback* callback = nullptr;
};

// ─── Decode Statistics ────────────────────────────────────────────────────────

struct AudioDecodeStats {
    uint64_t buffersSubmitted;
    uint64_t buffersProcessed;
    uint64_t samplesDecoded;
    double   decodeTimeAvgMs;
    uint32_t activeVoices;
    uint32_t poolCapacity;
};

// ─── XAudio2 Full Interface ───────────────────────────────────────────────────

class XAudio2Full {
public:
    XAudio2Full();
    ~XAudio2Full();

    // Initialize the audio engine. Creates WASAPI output or XAudio2 fallback.
    bool initialize(uint32_t sampleRate = kDefaultSampleRate,
                    uint32_t channels = 2);

    void shutdown();

    // ── Voice Management ──────────────────────────────────────────────────

    // Create a source voice from the pool. Returns kInvalidVoice on failure.
    VoiceHandle createSourceVoice(const VoiceParams& params);

    // Create a submix voice for effects routing.
    VoiceHandle createSubmixVoice(const VoiceParams& params);

    // Get the master voice handle (created during initialize).
    VoiceHandle getMasterVoice() const;

    // Destroy a voice and return it to the pool.
    void destroyVoice(VoiceHandle handle);

    // ── Playback Control ──────────────────────────────────────────────────

    bool submitBuffer(VoiceHandle handle, const AudioBuffer& buffer);
    bool flushBuffers(VoiceHandle handle);
    bool startVoice(VoiceHandle handle, uint32_t flags = 0);
    bool stopVoice(VoiceHandle handle, uint32_t flags = 0);
    bool pauseVoice(VoiceHandle handle);
    bool resumeVoice(VoiceHandle handle);

    // ── Voice Properties ──────────────────────────────────────────────────

    void setVolume(VoiceHandle handle, float volume);
    float getVolume(VoiceHandle handle) const;

    void setFrequencyRatio(VoiceHandle handle, float ratio);
    float getFrequencyRatio(VoiceHandle handle) const;

    void setChannelVolumes(VoiceHandle handle, const float* volumes, uint32_t count);

    // ── 3D Spatial Audio ──────────────────────────────────────────────────

    void setListener(const AudioListener& listener);
    void setEmitter(VoiceHandle handle, const AudioEmitter& emitter);
    void setDopplerFactor(float factor);

    // Apply 3D DSP to a voice based on current emitter/listener positions.
    void apply3DAudio(VoiceHandle handle);

    // ── Sample Rate Conversion ────────────────────────────────────────────

    // Set target output sample rate for SRC on this voice.
    void setOutputSampleRate(VoiceHandle handle, uint32_t sampleRate);

    // ── Statistics ────────────────────────────────────────────────────────

    AudioDecodeStats getStats() const;

    // ── Engine State ──────────────────────────────────────────────────────

    bool isInitialized() const;
    uint32_t getActiveVoiceCount() const;

    // Non-copyable
    XAudio2Full(const XAudio2Full&) = delete;
    XAudio2Full& operator=(const XAudio2Full&) = delete;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace audio
} // namespace dedsec