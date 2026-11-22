// DedSec Fork — Xbox→PC Input Adapter
// Implementation: GameInput primary + XInput fallback + Keyboard-only mode
// Built from scratch for the DedSec DX12 native pipeline.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_XBOX_INPUT_ADAPTER_IMPLEMENTATION
#include "xbox_input_adapter.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <chrono>

#pragma comment(lib, "xinput.lib")

namespace dedsec {
namespace input {

// ─── Deadzone math ────────────────────────────────────────────────────────────

static float applyRadialDeadzone(float x, float y, const DeadzoneConfig& cfg) {
    float mag = std::sqrt(x * x + y * y);
    if (mag < cfg.innerRadius) return 0.0f;
    float normalized = (mag - cfg.innerRadius) / (cfg.outerSaturation - cfg.innerRadius);
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    // Preserve direction, apply magnitude
    float scale = normalized / (mag > 0.0001f ? mag : 1.0f);
    return x * scale; // caller applies to both axes
}

static void applyDeadzone(float& x, float& y, const DeadzoneConfig& cfg) {
    if (!cfg.useRadial) {
        // Axial deadzone
        if (std::abs(x) < cfg.innerRadius) x = 0.0f;
        else x = std::copysign((std::abs(x) - cfg.innerRadius) / (1.0f - cfg.innerRadius), x);
        if (std::abs(y) < cfg.innerRadius) y = 0.0f;
        else y = std::copysign((std::abs(y) - cfg.innerRadius) / (1.0f - cfg.innerRadius), y);
    } else {
        float mag = std::sqrt(x * x + y * y);
        if (mag < cfg.innerRadius) { x = 0.0f; y = 0.0f; return; }
        float norm = std::clamp((mag - cfg.innerRadius) / (cfg.outerSaturation - cfg.innerRadius), 0.0f, 1.0f);
        float scale = norm / mag;
        x *= scale;
        y *= scale;
    }
}

// ─── Internal state ──────────────────────────────────────────────────────────

struct XboxInputAdapter::Impl {
    InputBackend backend = InputBackend::Auto;
    bool initialized = false;
    bool gameinput_available = false;
    bool xinput_available = false;
    DeadzoneConfig leftStickDZ = {0.15f, 0.95f, true};
    DeadzoneConfig rightStickDZ = {0.15f, 0.95f, true};
    KeyboardMapping kbMap = {};
    bool keyboardMode = false;
    std::chrono::steady_clock::time_point initTime;

    ControllerState pollXInput(uint32_t playerIndex) const {
        ControllerState state{};
        XINPUT_STATE xiState;
        DWORD result = XInputGetState(playerIndex, &xiState);
        if (result != ERROR_SUCCESS) {
            state.connected = false;
            return state;
        }
        state.connected = true;
        state.buttons = xiState.Gamepad.wButtons;
        state.leftTrigger = static_cast<float>(xiState.Gamepad.bLeftTrigger) / 255.0f;
        state.rightTrigger = static_cast<float>(xiState.Gamepad.bRightTrigger) / 255.0f;
        state.leftStickX = static_cast<float>(xiState.Gamepad.sThumbLX) / 32767.0f;
        state.leftStickY = static_cast<float>(xiState.Gamepad.sThumbLY) / 32767.0f;
        state.rightStickX = static_cast<float>(xiState.Gamepad.sThumbRX) / 32767.0f;
        state.rightStickY = static_cast<float>(xiState.Gamepad.sThumbRY) / 32767.0f;
        applyDeadzone(state.leftStickX, state.leftStickY, leftStickDZ);
        applyDeadzone(state.rightStickX, state.rightStickY, rightStickDZ);
        state.timestampUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - initTime).count());
        return state;
    }

    ControllerState pollKeyboard() const {
        ControllerState state{};
        state.connected = true;
        auto isDown = [](int vk) -> bool { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

        if (isDown(kbMap.keyA)) state.buttons |= static_cast<uint32_t>(XboxButton::A);
        if (isDown(kbMap.keyB)) state.buttons |= static_cast<uint32_t>(XboxButton::B);
        if (isDown(kbMap.keyX)) state.buttons |= static_cast<uint32_t>(XboxButton::X);
        if (isDown(kbMap.keyY)) state.buttons |= static_cast<uint32_t>(XboxButton::Y);
        if (isDown(kbMap.keyLB)) state.buttons |= static_cast<uint32_t>(XboxButton::LeftShoulder);
        if (isDown(kbMap.keyRB)) state.buttons |= static_cast<uint32_t>(XboxButton::RightShoulder);
        if (isDown(kbMap.keyStart)) state.buttons |= static_cast<uint32_t>(XboxButton::Start);
        if (isDown(kbMap.keyBack)) state.buttons |= static_cast<uint32_t>(XboxButton::Back);
        if (isDown(kbMap.keyLS)) state.buttons |= static_cast<uint32_t>(XboxButton::LeftThumb);
        if (isDown(kbMap.keyRS)) state.buttons |= static_cast<uint32_t>(XboxButton::RightThumb);
        if (isDown(kbMap.keyDPadUp)) state.buttons |= static_cast<uint32_t>(XboxButton::DPadUp);
        if (isDown(kbMap.keyDPadDown)) state.buttons |= static_cast<uint32_t>(XboxButton::DPadDown);
        if (isDown(kbMap.keyDPadLeft)) state.buttons |= static_cast<uint32_t>(XboxButton::DPadLeft);
        if (isDown(kbMap.keyDPadRight)) state.buttons |= static_cast<uint32_t>(XboxButton::DPadRight);
        if (isDown(kbMap.keyLT)) state.leftTrigger = 1.0f;
        if (isDown(kbMap.keyRT)) state.rightTrigger = 1.0f;

        // Left stick from keys
        if (isDown(kbMap.keyLStickUp)) state.leftStickY = 1.0f;
        if (isDown(kbMap.keyLStickDown)) state.leftStickY = -1.0f;
        if (isDown(kbMap.keyLStickLeft)) state.leftStickX = -1.0f;
        if (isDown(kbMap.keyLStickRight)) state.leftStickX = 1.0f;
        // Right stick from keys
        if (isDown(kbMap.keyRStickUp)) state.rightStickY = 1.0f;
        if (isDown(kbMap.keyRStickDown)) state.rightStickY = -1.0f;
        if (isDown(kbMap.keyRStickLeft)) state.rightStickX = -1.0f;
        if (isDown(kbMap.keyRStickRight)) state.rightStickX = 1.0f;

        state.timestampUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - initTime).count());
        return state;
    }
};

// ─── Public API ───────────────────────────────────────────────────────────────

XboxInputAdapter::XboxInputAdapter() : impl_(new Impl()) {}
XboxInputAdapter::~XboxInputAdapter() { delete impl_; }

bool XboxInputAdapter::initialize(InputBackend backend) {
    impl_->backend = backend;
    impl_->initTime = std::chrono::steady_clock::now();
    impl_->kbMap = getDefaultKeyboardMapping();

    switch (backend) {
    case InputBackend::XInput:
        impl_->xinput_available = true;
        impl_->initialized = true;
        std::printf("[DedSec Input] XInput backend initialized\n");
        return true;
    case InputBackend::Keyboard:
        impl_->keyboardMode = true;
        impl_->initialized = true;
        std::printf("[DedSec Input] Keyboard-only backend initialized\n");
        return true;
    case InputBackend::GameInput:
        // TODO: Load GameInput.dll dynamically when available
        // For now, fall through to Auto behavior
        std::printf("[DedSec Input] GameInput requested but not yet implemented, falling back to XInput\n");
        [[fallthrough]];
    case InputBackend::Auto:
    default:
        // Try XInput as baseline (always available on Windows)
        impl_->xinput_available = true;
        impl_->initialized = true;
        std::printf("[DedSec Input] Auto backend: XInput active (GameInput pending)\n");
        return true;
    }
}

void XboxInputAdapter::shutdown() {
    impl_->initialized = false;
    impl_->gameinput_available = false;
    impl_->xinput_available = false;
    impl_->keyboardMode = false;
}

bool XboxInputAdapter::isConnected(uint32_t playerIndex) const {
    if (!impl_->initialized) return false;
    if (impl_->keyboardMode) return true;
    if (impl_->xinput_available) {
        XINPUT_STATE state;
        return XInputGetState(playerIndex, &state) == ERROR_SUCCESS;
    }
    return false;
}

ControllerState XboxInputAdapter::getState(uint32_t playerIndex) const {
    if (!impl_->initialized) return ControllerState{};
    if (impl_->keyboardMode) return impl_->pollKeyboard();
    if (impl_->xinput_available) return impl_->pollXInput(playerIndex);
    return ControllerState{};
}

bool XboxInputAdapter::setVibration(uint32_t playerIndex, const VibrationState& vibration) {
    if (!impl_->initialized || !impl_->xinput_available) return false;
    XINPUT_VIBRATION vib{};
    vib.wLeftMotorSpeed = static_cast<WORD>(vibration.leftMotor * 65535.0f);
    vib.wRightMotorSpeed = static_cast<WORD>(vibration.rightMotor * 65535.0f);
    return XInputSetState(playerIndex, &vib) == ERROR_SUCCESS;
    // Note: trigger rumble requires GameInput API (not XInput)
}

void XboxInputAdapter::setDeadzone(bool rightStick, const DeadzoneConfig& config) {
    if (rightStick) impl_->rightStickDZ = config;
    else impl_->leftStickDZ = config;
}

const char* XboxInputAdapter::getBackendName() const {
    if (impl_->keyboardMode) return "Keyboard";
    if (impl_->gameinput_available) return "GameInput";
    if (impl_->xinput_available) return "XInput";
    return "None";
}

uint32_t XboxInputAdapter::getControllerCount() const {
    if (!impl_->initialized) return 0;
    if (impl_->keyboardMode) return 1;
    uint32_t count = 0;
    for (uint32_t i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state;
        if (XInputGetState(i, &state) == ERROR_SUCCESS) count++;
    }
    return count;
}

KeyboardMapping getDefaultKeyboardMapping() {
    KeyboardMapping m{};
    m.keyA = 'Z';          // A button
    m.keyB = 'X';          // B button
    m.keyX = 'A';          // X button
    m.keyY = 'S';          // Y button
    m.keyLB = 'Q';         // Left bumper
    m.keyRB = 'E';         // Right bumper
    m.keyStart = VK_RETURN;
    m.keyBack = VK_BACK;
    m.keyLS = 'C';
    m.keyRS = 'V';
    m.keyDPadUp = VK_UP;
    m.keyDPadDown = VK_DOWN;
    m.keyDPadLeft = VK_LEFT;
    m.keyDPadRight = VK_RIGHT;
    m.keyLT = VK_LSHIFT;
    m.keyRT = VK_RSHIFT;
    m.keyLStickUp = 'W';
    m.keyLStickDown = 'S';  // conflicts with Y; user can remap
    m.keyLStickLeft = 'A';  // conflicts with X; user can remap
    m.keyLStickRight = 'D';
    m.keyRStickUp = VK_NUMPAD8;
    m.keyRStickDown = VK_NUMPAD2;
    m.keyRStickLeft = VK_NUMPAD4;
    m.keyRStickRight = VK_NUMPAD6;
    return m;
}

} // namespace input
} // namespace dedsec