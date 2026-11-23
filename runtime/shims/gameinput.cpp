// DedSec Fork — GameInput / KBM Mapping Layer Implementation (Sprint 4)
// Clean-room implementation of the GameInputManager declared in gameinput.h.
//   - RawInput registration for mouse + keyboard (RIDEV_INPUTSINK)
//   - Lock-free SPSC ring buffer producer from WM_INPUT wndproc
//   - WASD → left stick, mouse delta → right stick virtual controller
//   - Hot-plug via WM_INPUTDEVICECHANGE with user callback
//   - QPC timestamps on every event, zero heap allocation in poll path
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "gameinput.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <hidsdi.h>
#include <cmath>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "hid.lib")

namespace dedsec {
namespace input {

// ─── Internal implementation ──────────────────────────────────────────────────

struct GameInputManager::Impl {
    bool initialized = false;
    HWND messageWindow = nullptr;
    bool ownsWindow = false;

    KbmToControllerMap kbmMap = {};

    // Accumulated mouse deltas (written by producer, read by consumer).
    // Using atomics for cross-thread visibility without locks.
    std::atomic<int32_t> mouseDeltaX{0};
    std::atomic<int32_t> mouseDeltaY{0};

    // Current WASD key state sampled via GetAsyncKeyState on the game thread.
    // These are not shared across threads — only read on the consumer side.
    mutable float wasmStickX = 0.0f;
    mutable float wasmStickY = 0.0f;

    DeviceCallback deviceCb = nullptr;
    void* deviceCbCtx = nullptr;

    XboxInputAdapter adapter;

    uint64_t qpcFrequency = 1;

    static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

// ─── WndProc for RawInput messages ────────────────────────────────────────────

LRESULT CALLBACK GameInputManager::Impl::MessageWndProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INPUT) {
        HRAWINPUT hRaw = reinterpret_cast<HRAWINPUT>(lParam);
        GameInputManager::instance().processRawInputMessage(hRaw);
        return 0;
    }
    if (msg == WM_INPUTDEVICECHANGE) {
        GameInputManager::instance().processDeviceChange(wParam, lParam);
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── Singleton ────────────────────────────────────────────────────────────────

GameInputManager& GameInputManager::instance() {
    static GameInputManager s_instance;
    return s_instance;
}

GameInputManager::GameInputManager() : impl_(new Impl()) {}

GameInputManager::~GameInputManager() {
    shutdown();
    delete impl_;
}

// ─── Initialization ──────────────────────────────────────────────────────────

bool GameInputManager::initialize(HWND hwnd) {
    if (impl_->initialized) return true;

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    impl_->qpcFrequency = static_cast<uint64_t>(freq.QuadPart);

    // Create a hidden message-only window if none provided.
    if (!hwnd) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::MessageWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DedSecGameInputMsgWnd";
        RegisterClassExW(&wc);

        impl_->messageWindow = CreateWindowExW(
            0, wc.lpszClassName, L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

        if (!impl_->messageWindow) return false;
        impl_->ownsWindow = true;
        hwnd = impl_->messageWindow;
    } else {
        impl_->messageWindow = hwnd;
        impl_->ownsWindow = false;
    }

    // Register RawInput devices: mouse + keyboard with RIDEV_INPUTSINK
    // so we receive input even when the window is not focused.
    RAWINPUTDEVICE rid[2] = {};

    rid[0].usUsagePage = 0x01;  // Generic Desktop
    rid[0].usUsage     = 0x02;  // Mouse
    rid[0].dwFlags     = RIDEV_INPUTSINK;
    rid[0].hwndTarget  = hwnd;

    rid[1].usUsagePage = 0x01;  // Generic Desktop
    rid[1].usUsage     = 0x06;  // Keyboard
    rid[1].dwFlags     = RIDEV_INPUTSINK;
    rid[1].hwndTarget  = hwnd;

    if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
        return false;
    }

    // Initialize the underlying XInput adapter.
    impl_->adapter.initialize();

    impl_->initialized = true;
    return true;
}

void GameInputManager::shutdown() {
    if (!impl_->initialized) return;

    // Unregister RawInput by setting RIDEV_REMOVE.
    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage     = 0x02;
    rid[0].dwFlags     = RIDEV_REMOVE;
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage     = 0x06;
    rid[1].dwFlags     = RIDEV_REMOVE;
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    if (impl_->ownsWindow && impl_->messageWindow) {
        DestroyWindow(impl_->messageWindow);
        impl_->messageWindow = nullptr;
    }

    impl_->adapter.shutdown();
    impl_->initialized = false;
}

// ─── Event polling (consumer / game thread) ───────────────────────────────────

size_t GameInputManager::pollEvents(InputEvent* outBuffer, size_t maxEvents) {
    return ring_.drain(outBuffer, maxEvents);
}

// ─── Virtual controller merge ─────────────────────────────────────────────────

ControllerState GameInputManager::getVirtualController(uint32_t playerIndex) const {
    // Start with live XInput state.
    ControllerState state = impl_->adapter.getState(playerIndex);

    // Sample WASD keys and produce left-stick axes.
    float wasmX = 0.0f, wasmY = 0.0f;
    sampleWasmStick(wasmX, wasmY);

    // If no physical controller is connected, or WASD is active, blend in KBM.
    if (!state.connected || (wasmX != 0.0f || wasmY != 0.0f)) {
        state.connected = true;
        // Only override left stick if WASD has input.
        if (wasmX != 0.0f || wasmY != 0.0f) {
            state.leftStickX = wasmX;
            state.leftStickY = wasmY;
        }
    }

    // Mouse look → right stick.
    if (impl_->kbmMap.mouseLookEnabled) {
        int32_t dx = impl_->mouseDeltaX.exchange(0, std::memory_order_relaxed);
        int32_t dy = impl_->mouseDeltaY.exchange(0, std::memory_order_relaxed);

        float rx = applyMouseAxis(static_cast<float>(dx),
                                  impl_->kbmMap.mouseSensitivityX,
                                  impl_->kbmMap.mouseDeadzone);
        float ry = applyMouseAxis(static_cast<float>(dy),
                                  impl_->kbmMap.mouseSensitivityY,
                                  impl_->kbmMap.mouseDeadzone);

        // Blend: take the larger magnitude between physical and virtual.
        if (std::abs(rx) > std::abs(state.rightStickX)) state.rightStickX = rx;
        if (std::abs(ry) > std::abs(state.rightStickY)) state.rightStickY = ry;
    }

    return state;
}

// ─── Configuration ────────────────────────────────────────────────────────────

void GameInputManager::setKbmMapping(const KbmToControllerMap& map) {
    impl_->kbmMap = map;
}

void GameInputManager::setDeviceCallback(DeviceCallback cb, void* ctx) {
    impl_->deviceCb = cb;
    impl_->deviceCbCtx = ctx;
}

// ─── Producer-side: RawInput processing ───────────────────────────────────────

void GameInputManager::processRawInputMessage(HRAWINPUT hRawInput) {
    UINT size = 0;
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0 || size > 4096) return;

    // Stack-allocated buffer — zero heap allocation.
    alignas(RAWINPUT) char buf[4096];
    if (GetRawInputData(hRawInput, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }

    auto* raw = reinterpret_cast<RAWINPUT*>(buf);

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    uint64_t timestamp = static_cast<uint64_t>(qpc.QuadPart);

    if (raw->header.dwType == RIM_TYPEMOUSE) {
        const auto& mouse = raw->data.mouse;

        // Accumulate deltas for the consumer to read atomically.
        if (mouse.lLastX != 0) {
            impl_->mouseDeltaX.fetch_add(mouse.lLastX, std::memory_order_relaxed);
        }
        if (mouse.lLastY != 0) {
            impl_->mouseDeltaY.fetch_add(mouse.lLastY, std::memory_order_relaxed);
        }

        // Push a MouseMove event into the ring buffer.
        if (mouse.lLastX != 0 || mouse.lLastY != 0) {
            InputEvent ev{};
            ev.type = InputEvent::Type::MouseMove;
            ev.deviceId = 0; // mouse
            ev.timestampQpc = timestamp;
            ev.data.mouse.dx = mouse.lLastX;
            ev.data.mouse.dy = mouse.lLastY;
            ring_.push(ev);
        }

        // Push button events for mouse buttons.
        constexpr struct { USHORT flag; uint32_t buttonId; } kMouseButtons[] = {
            { RI_MOUSE_LEFT_BUTTON_DOWN,   0 },
            { RI_MOUSE_RIGHT_BUTTON_DOWN,  1 },
            { RI_MOUSE_MIDDLE_BUTTON_DOWN, 2 },
            { RI_MOUSE_BUTTON_4_DOWN,      3 },
            { RI_MOUSE_BUTTON_5_DOWN,      4 },
        };
        for (const auto& mb : kMouseButtons) {
            if (mouse.usButtonFlags & mb.flag) {
                InputEvent ev{};
                ev.type = InputEvent::Type::Button;
                ev.deviceId = 0;
                ev.timestampQpc = timestamp;
                ev.data.button.button = mb.buttonId;
                ev.data.button.pressed = true;
                ring_.push(ev);
            }
            // Check release flags (shifted by 1 in usButtonFlags for some, but
            // RawInput uses separate UP flags for left/right/middle).
            USHORT upFlag = 0;
            switch (mb.flag) {
                case RI_MOUSE_LEFT_BUTTON_DOWN:   upFlag = RI_MOUSE_LEFT_BUTTON_UP; break;
                case RI_MOUSE_RIGHT_BUTTON_DOWN:  upFlag = RI_MOUSE_RIGHT_BUTTON_UP; break;
                case RI_MOUSE_MIDDLE_BUTTON_DOWN: upFlag = RI_MOUSE_MIDDLE_BUTTON_UP; break;
                default: break;
            }
            if (upFlag && (mouse.usButtonFlags & upFlag)) {
                InputEvent ev{};
                ev.type = InputEvent::Type::Button;
                ev.deviceId = 0;
                ev.timestampQpc = timestamp;
                ev.data.button.button = mb.buttonId;
                ev.data.button.pressed = false;
                ring_.push(ev);
            }
        }
    } else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const auto& kb = raw->data.keyboard;

        InputEvent ev{};
        ev.type = InputEvent::Type::Button;
        ev.deviceId = 1; // keyboard
        ev.timestampQpc = timestamp;
        ev.data.button.button = kb.VKey;
        ev.data.button.pressed = !(kb.Flags & RI_KEY_BREAK);
        ring_.push(ev);
    }
}

void GameInputManager::processDeviceChange(WPARAM wParam, LPARAM /*lParam*/) {
    if (!impl_->deviceCb) return;

    bool connected = (wParam == GIDC_ARRIVAL);
    // Use a synthetic device ID based on arrival/removal count.
    static uint32_t s_nextDeviceId = 100;
    uint32_t deviceId = connected ? s_nextDeviceId++ : 0;

    impl_->deviceCb(deviceId, connected, impl_->deviceCbCtx);

    // Also push an event into the ring buffer for the game thread.
    InputEvent ev{};
    ev.type = connected ? InputEvent::Type::DeviceConnected
                        : InputEvent::Type::DeviceDisconnected;
    ev.deviceId = deviceId;
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    ev.timestampQpc = static_cast<uint64_t>(qpc.QuadPart);
    ring_.push(ev);
}

bool GameInputManager::pushEvent(const InputEvent& event) {
    return ring_.push(event);
}

// ─── Private helpers ──────────────────────────────────────────────────────────

void GameInputManager::sampleWasmStick(float& outX, float& outY) const {
    outX = 0.0f;
    outY = 0.0f;

    const auto& map = impl_->kbmMap;

    // GetAsyncKeyState returns negative high bit if currently pressed.
    auto isDown = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    if (isDown(map.keyRight)) outX += 1.0f;
    if (isDown(map.keyLeft))  outX -= 1.0f;
    if (isDown(map.keyForward)) outY += 1.0f;
    if (isDown(map.keyBack))    outY -= 1.0f;

    // Normalize diagonal movement to unit circle.
    float mag = std::sqrt(outX * outX + outY * outY);
    if (mag > 1.0f) {
        outX /= mag;
        outY /= mag;
    }
}

float GameInputManager::applyMouseAxis(float raw, float sensitivity, float deadzone) {
    float val = raw * sensitivity;
    if (std::abs(val) < deadzone) return 0.0f;
    // Clamp to [-1, 1] range for stick emulation.
    return std::clamp(val, -1.0f, 1.0f);
}

} // namespace input
} // namespace dedsec