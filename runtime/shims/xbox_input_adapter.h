// DedSec Fork — Xbox→PC Input Adapter
// Adapts Xbox Series X|S GameInput API to Windows PC (GameInput + XInput fallback)
// Supports: buttons, triggers, thumbsticks, D-pad, vibration, radial deadzone
//
// Architecture:
//   Xbox GameInput → dedsec::XboxInputAdapter → Windows GameInput / XInput
//   Transparent fallback: if GameInput unavailable, uses XInput automatically
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <cstring>

namespace dedsec {
namespace input {

// Xbox controller button flags (matching XINPUT_GAMEPAD_*)
enum class XboxButton : uint32_t {
    DPadUp        = 0x0001,
    DPadDown      = 0x0002,
    DPadLeft      = 0x0004,
    DPadRight     = 0x0008,
    Start         = 0x0010,
    Back          = 0x0020,
    LeftThumb     = 0x0040,
    RightThumb    = 0x0080,
    LeftShoulder  = 0x0100,
    RightShoulder = 0x0200,
    A             = 0x1000,
    B             = 0x2000,
    X             = 0x4000,
    Y             = 0x8000,
};

// Normalized controller state (-1.0 to 1.0 for sticks, 0.0 to 1.0 for triggers)
struct ControllerState {
    float leftStickX;
    float leftStickY;
    float rightStickX;
    float rightStickY;
    float leftTrigger;
    float rightTrigger;
    uint32_t buttons;       // bitmask of XboxButton values
    bool connected;
    uint64_t timestampUs;   // microseconds since adapter init
};

// Vibration motor intensities (0.0 to 1.0)
struct VibrationState {
    float leftMotor;
    float rightMotor;
    float leftTriggerMotor;   // Xbox One S+ / Series trigger rumble
    float rightTriggerMotor;
};

// Radial deadzone configuration (per-stick)
struct DeadzoneConfig {
    float innerRadius;      // 0.0 to 1.0, default 0.15
    float outerSaturation;  // 0.0 to 1.0, default 0.95
    bool useRadial;         // true = circular deadzone, false = axial (square)
};

// Input backend selection
enum class InputBackend : uint8_t {
    Auto = 0,       // Try GameInput first, fallback to XInput
    GameInput = 1,  // Force Windows GameInput API
    XInput = 2,     // Force legacy XInput
    Keyboard = 3,   // Keyboard-only fallback (for testing without controller)
};

class XboxInputAdapter {
public:
    XboxInputAdapter();
    ~XboxInputAdapter();

    // Initialize the adapter with preferred backend
    bool initialize(InputBackend backend = InputBackend::Auto);

    // Shutdown and release resources
    void shutdown();

    // Check if a controller is connected
    bool isConnected(uint32_t playerIndex = 0) const;

    // Get current controller state (normalized -1..1 sticks, 0..1 triggers)
    ControllerState getState(uint32_t playerIndex = 0) const;

    // Set vibration motors
    bool setVibration(uint32_t playerIndex, const VibrationState& vibration);

    // Configure deadzone for a specific stick
    void setDeadzone(bool rightStick, const DeadzoneConfig& config);

    // Get active backend name
    const char* getBackendName() const;

    // Get number of connected controllers
    uint32_t getControllerCount() const;

    // Non-copyable
    XboxInputAdapter(const XboxInputAdapter&) = delete;
    XboxInputAdapter& operator=(const XboxInputAdapter&) = delete;

private:
    struct Impl;
    Impl* impl_;
};

// Keyboard mapping for testing without a controller
struct KeyboardMapping {
    int keyA;           // VK code for A button
    int keyB;           // VK code for B button
    int keyX;           // VK code for X button
    int keyY;           // VK code for Y button
    int keyLB;          // Left bumper
    int keyRB;          // Right bumper
    int keyStart;       // Start/Menu
    int keyBack;        // Back/View
    int keyLS;          // Left stick click
    int keyRS;          // Right stick click
    int keyDPadUp;
    int keyDPadDown;
    int keyDPadLeft;
    int keyDPadRight;
    int keyLT;          // Left trigger (hold = 1.0)
    int keyRT;          // Right trigger (hold = 1.0)
    int keyLStickUp;
    int keyLStickDown;
    int keyLStickLeft;
    int keyLStickRight;
    int keyRStickUp;
    int keyRStickDown;
    int keyRStickLeft;
    int keyRStickRight;
};

// Default WASD + arrow keys mapping
KeyboardMapping getDefaultKeyboardMapping();

} // namespace input
} // namespace dedsec