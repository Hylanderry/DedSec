// DedSec Fork — GameInput / KBM Mapping Layer (Sprint 4)
// High-level input manager above XboxInputAdapter:
//   - Lock-free SPSC ring buffer for raw input events (zero-alloc poll)
//   - RawInput mouse look with QPC timestamps
//   - Hot-plug detection via WM_INPUTDEVICECHANGE
//   - KBM → virtual controller mapping (WASD + mouse → sticks)
//
// Architecture:
//   Win32 RawInput / XInput → GameInputManager → ring buffer → game thread
//   GameInputManager merges XInput state + KBM virtual controller each frame.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Forward-declare HWND to avoid pulling windows.h into every translation unit
// that includes this header. The .cpp includes <windows.h> directly.
struct HWND__;
using HWND = HWND__*;

#include "xbox_input_adapter.h"

namespace dedsec {
namespace input {

// ─── Input event (fixed-size, trivially copyable for ring buffer) ──────────────

struct InputEvent {
    enum class Type : uint8_t {
        Button,
        Axis,
        MouseMove,
        DeviceConnected,
        DeviceDisconnected
    };

    Type     type;
    uint32_t deviceId;
    uint64_t timestampQpc;   // QueryPerformanceCounter raw ticks

    union Data {
        struct { uint32_t button; bool pressed; } button;
        struct { float x, y; }                   axis;
        struct { int32_t dx, dy; }                mouse;
    } data;
};

static_assert(std::is_trivially_copyable_v<InputEvent>,
              "InputEvent must be trivially copyable for lock-free ring buffer");

// ─── Lock-free SPSC ring buffer ────────────────────────────────────────────────
// Single producer (RawInput wndproc thread) → single consumer (game thread).
// Capacity MUST be a power of two so we can use bitwise AND as modulo.

template <size_t Capacity>
class InputRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "InputRingBuffer capacity must be a power of two");
public:
    InputRingBuffer() : head_(0), tail_(0) {}

    // Producer: push one event. Returns false if the buffer is full.
    bool push(const InputEvent& event) noexcept {
        const size_t curHead = head_.load(std::memory_order_relaxed);
        const size_t nextHead = (curHead + 1) & kMask;
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[curHead] = event;
        head_.store(nextHead, std::memory_order_release);
        return true;
    }

    // Consumer: pop one event. Returns false if the buffer is empty.
    bool pop(InputEvent& out) noexcept {
        const size_t curTail = tail_.load(std::memory_order_relaxed);
        if (curTail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buffer_[curTail];
        tail_.store((curTail + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Consumer: drain up to maxEvents into caller-provided buffer.
    // Returns number of events actually copied. Zero heap allocation.
    size_t drain(InputEvent* outBuffer, size_t maxEvents) noexcept {
        size_t count = 0;
        while (count < maxEvents && pop(outBuffer[count])) {
            ++count;
        }
        return count;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    size_t capacity() const noexcept { return Capacity; }

private:
    static constexpr size_t kMask = Capacity - 1;

    // Pad to avoid false sharing between producer and consumer cache lines.
    alignas(64) std::atomic<size_t> head_;
    char pad0_[64 - sizeof(std::atomic<size_t>)];
    alignas(64) std::atomic<size_t> tail_;
    char pad1_[64 - sizeof(std::atomic<size_t>)];

    InputEvent buffer_[Capacity];
};

// Default ring buffer size used by GameInputManager.
static constexpr size_t kDefaultRingCapacity = 256;

// ─── KBM → virtual controller mapping configuration ───────────────────────────

struct KbmToControllerMap {
    // Mouse sensitivity multipliers applied to raw delta before accumulation.
    float mouseSensitivityX = 1.0f;
    float mouseSensitivityY = 1.0f;

    // Minimum absolute delta (after sensitivity) to register as movement.
    float mouseDeadzone = 0.001f;

    // VK codes for WASD movement → left stick axes.
    int keyForward = 'W';
    int keyBack    = 'S';
    int keyLeft    = 'A';
    int keyRight   = 'D';

    // When true, accumulated mouse deltas feed the right stick.
    bool mouseLookEnabled = true;
};

// ─── GameInputManager ──────────────────────────────────────────────────────────
// Singleton that owns the RawInput registration, ring buffer, and virtual
// controller merge logic. Thread-safety contract:
//   - initialize() / shutdown() / setKbmMapping() / setDeviceCallback():
//     call from main thread only (before polling starts or after shutdown).
//   - processRawInputMessage(): call ONLY from the window procedure that
//     receives WM_INPUT / WM_INPUTDEVICECHANGE (the producer side).
//   - pollEvents() / getVirtualController(): call ONLY from the game thread
//     (the consumer side).

class GameInputManager {
public:
    // Register RawInput devices (mouse + keyboard with RIDEV_INPUTSINK).
    // If hwnd is nullptr, a hidden message-only window is created internally.
    bool initialize(HWND hwnd = nullptr);

    // Unregister RawInput devices and release internal resources.
    void shutdown();

    // Drain pending events from the ring buffer into caller-owned memory.
    // Returns the number of events written (≤ maxEvents). Zero allocation.
    size_t pollEvents(InputEvent* outBuffer, size_t maxEvents);

    // Build an aggregated ControllerState by merging:
    //   1. Live XInput state from XboxInputAdapter
    //   2. Accumulated mouse deltas → right stick (if mouseLookEnabled)
    //   3. WASD key state → left stick
    // Call once per frame on the game thread.
    ControllerState getVirtualController(uint32_t playerIndex = 0) const;

    // Replace the current KBM mapping configuration.
    void setKbmMapping(const KbmToControllerMap& map);

    // Hot-plug callback: fired when a device connects or disconnects.
    using DeviceCallback = void (*)(uint32_t deviceId, bool connected, void* userCtx);
    void setDeviceCallback(DeviceCallback cb, void* ctx);

    // ── Producer-side API (call from wndproc) ──────────────────────────────
    // Process a WM_INPUT message and push events into the ring buffer.
    void processRawInputMessage(HRAWINPUT hRawInput);

    // Process WM_INPUTDEVICECHANGE and fire the hot-plug callback.
    void processDeviceChange(WPARAM wParam, LPARAM lParam);

    // Push an arbitrary event (for testing or external producers).
    bool pushEvent(const InputEvent& event);

    // Access the underlying ring buffer (testing / diagnostics).
    InputRingBuffer<kDefaultRingCapacity>& ringBuffer() { return ring_; }

    // Singleton accessor.
    static GameInputManager& instance();

    // Non-copyable, non-movable.
    GameInputManager(const GameInputManager&) = delete;
    GameInputManager& operator=(const GameInputManager&) = delete;

private:
    GameInputManager();
    ~GameInputManager();

    // Read current WASD key state and produce normalized left-stick values.
    void sampleWasmStick(float& outX, float& outY) const;

    // Apply mouse deadzone and sensitivity to an accumulated delta.
    static float applyMouseAxis(float raw, float sensitivity, float deadzone);

    struct Impl;
    Impl* impl_;

    InputRingBuffer<kDefaultRingCapacity> ring_;
};

} // namespace input
} // namespace dedsec