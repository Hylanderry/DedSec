// DedSec Fork — XAudio2 Shim for PC
// Reimplements Xbox Series XAudio2 API using Windows XAudio2 or WASAPI fallback.
// Clean-room implementation based on public XAudio2 documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

namespace dedsec {
namespace audio {

// XAudio2 format tags (matching Xbox SDK)
enum class AudioFormatTag : uint16_t {
    PCM = 1,
    ADPCM = 2,
    XMA2 = 0x166,
    WMAPro = 0x163,
    Opus = 0x704F
};

// Speaker configuration flags
enum class SpeakerFlags : uint32_t {
    Mono = 0x00000001,
    Stereo = 0x00000003,
    Surround5_1 = 0x0000003F,
    Surround7_1 = 0x0000063F
};

// Voice state flags
enum class VoiceState : uint32_t {
    Playing = 0x00000001,
    Paused = 0x00000002,
    Stopped = 0x00000004,
    EndOfStream = 0x00000008
};

// Audio buffer descriptor (matches XAUDIO2_BUFFER)
struct AudioBuffer {
    uint32_t flags;
    uint32_t audioBytes;
    const uint8_t* pAudioData;
    uint32_t playBegin;
    uint32_t playLength;
    uint32_t loopBegin;
    uint32_t loopLength;
    uint32_t loopCount;
    void* pContext;
};

// Wave format extended (matches WAVEFORMATEXTENSIBLE)
struct WaveFormatEx {
    AudioFormatTag formatTag;
    uint16_t channels;
    uint32_t samplesPerSec;
    uint32_t avgBytesPerSec;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint16_t cbSize;
    uint16_t validBitsPerSample;
    uint32_t channelMask;
    uint8_t subFormat[16]; // GUID
};

// Voice send descriptor
struct VoiceSendDescriptor {
    uint32_t flags;
    void* pOutputVoice;
};

// Volume levels per channel (up to 8)
struct VoiceVolume {
    float masterVolume;
    float channelVolumes[8];
    uint32_t channelCount;
};

// Forward declarations
class IXAudio2Voice;
class IXAudio2SourceVoice;
class IXAudio2MasteringVoice;

// Main XAudio2 interface
class IXAudio2 {
public:
    virtual ~IXAudio2() = default;

    virtual bool initialize(uint32_t flags = 0, uint32_t processor = 0) = 0;
    virtual void shutdown() = 0;

    virtual IXAudio2MasteringVoice* createMasteringVoice(
        uint32_t inputChannels = 0,
        uint32_t inputSampleRate = 0) = 0;

    virtual IXAudio2SourceVoice* createSourceVoice(
        const WaveFormatEx* sourceFormat,
        uint32_t flags = 0,
        float maxFrequencyRatio = 2.0f) = 0;

    virtual void update() = 0;
    virtual uint32_t getProcessingQuantum() const = 0;
};

// Source voice interface
class IXAudio2SourceVoice {
public:
    virtual ~IXAudio2SourceVoice() = default;

    virtual bool submitSourceBuffer(const AudioBuffer* pBuffer) = 0;
    virtual bool flushSourceBuffers() = 0;
    virtual bool start(uint32_t flags = 0) = 0;
    virtual bool stop(uint32_t flags = 0) = 0;
    virtual bool pause() = 0;

    virtual void setVolume(float volume) = 0;
    virtual float getVolume() const = 0;
    virtual void setChannelVolumes(uint32_t channels, const float* volumes) = 0;
    virtual void getChannelVolumes(uint32_t channels, float* volumes) const = 0;

    virtual void setFrequencyRatio(float ratio) = 0;
    virtual float getFrequencyRatio() const = 0;

    virtual VoiceState getState() const = 0;
    virtual uint32_t getBuffersQueued() const = 0;
    virtual uint64_t getSamplesPlayed() const = 0;

    virtual void destroy() = 0;
};

// Mastering voice interface
class IXAudio2MasteringVoice {
public:
    virtual ~IXAudio2MasteringVoice() = default;

    virtual void setVolume(float volume) = 0;
    virtual float getVolume() const = 0;
    virtual void destroy() = 0;
};

// Factory function
IXAudio2* createXAudio2();

// Utility functions
bool isXAudio2Available();
std::string getAudioBackendName();

} // namespace audio
} // namespace dedsec