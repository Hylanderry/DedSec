// DedSec Fork — Xbox→PC Input Adapter Unit Test
// Validates initialization, backend selection, deadzone math, and state polling.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "../shims/xbox_input_adapter.h"

#include <cstdio>
#include <cmath>
#include <string>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    if (std::abs((a) - (b)) > (eps)) { \
        std::fprintf(stderr, "FAIL line %d: |%f - %f| > %f\n", __LINE__, \
                     static_cast<double>(a), static_cast<double>(b), \
                     static_cast<double>(eps)); \
        return 1; \
    } \
} while (0)

using namespace dedsec::input;

int main() {
    std::printf("=== DedSec Xbox Input Adapter Test ===\n");

    // ── Test 1: Default construction does not crash ────────────────────────
    {
        XboxInputAdapter adapter;
        CHECK(!adapter.isConnected());
        std::printf("[PASS] Default construction safe\n");
    }

    // ── Test 2: Initialize with XInput backend ────────────────────────────
    {
        XboxInputAdapter adapter;
        bool ok = adapter.initialize(InputBackend::XInput);
        CHECK(ok);
        CHECK(std::string(adapter.getBackendName()) == "XInput");
        adapter.shutdown();
        std::printf("[PASS] XInput backend init\n");
    }

    // ── Test 3: Initialize with Keyboard backend ──────────────────────────
    {
        XboxInputAdapter adapter;
        bool ok = adapter.initialize(InputBackend::Keyboard);
        CHECK(ok);
        CHECK(std::string(adapter.getBackendName()) == "Keyboard");
        // Keyboard mode always reports connected
        CHECK(adapter.isConnected());
        adapter.shutdown();
        std::printf("[PASS] Keyboard backend init\n");
    }

    // ── Test 4: Initialize with Auto backend ──────────────────────────────
    {
        XboxInputAdapter adapter;
        bool ok = adapter.initialize(InputBackend::Auto);
        CHECK(ok);
        // Auto should resolve to XInput (GameInput not yet implemented)
        std::string name = adapter.getBackendName();
        CHECK(name == "XInput" || name == "GameInput");
        adapter.shutdown();
        std::printf("[PASS] Auto backend init → %s\n", name.c_str());
    }

    // ── Test 5: Shutdown is idempotent ────────────────────────────────────
    {
        XboxInputAdapter adapter;
        adapter.initialize(InputBackend::Keyboard);
        adapter.shutdown();
        adapter.shutdown(); // second call should not crash
        CHECK(!adapter.isConnected());
        std::printf("[PASS] Shutdown idempotent\n");
    }

    // ── Test 6: getState returns zeros when not initialized ───────────────
    {
        XboxInputAdapter adapter;
        auto state = adapter.getState();
        CHECK(!state.connected);
        CHECK_NEAR(state.leftStickX, 0.0f, 0.001f);
        CHECK_NEAR(state.leftStickY, 0.0f, 0.001f);
        CHECK_NEAR(state.rightStickX, 0.0f, 0.001f);
        CHECK_NEAR(state.rightStickY, 0.0f, 0.001f);
        CHECK_NEAR(state.leftTrigger, 0.0f, 0.001f);
        CHECK_NEAR(state.rightTrigger, 0.0f, 0.001f);
        CHECK(state.buttons == 0);
        std::printf("[PASS] Uninitialized state is zeroed\n");
    }

    // ── Test 7: Keyboard mode returns connected state ─────────────────────
    {
        XboxInputAdapter adapter;
        adapter.initialize(InputBackend::Keyboard);
        auto state = adapter.getState();
        CHECK(state.connected);
        CHECK(state.timestampUs > 0);
        adapter.shutdown();
        std::printf("[PASS] Keyboard mode returns connected state\n");
    }

    // ── Test 8: getControllerCount in keyboard mode ───────────────────────
    {
        XboxInputAdapter adapter;
        adapter.initialize(InputBackend::Keyboard);
        CHECK(adapter.getControllerCount() == 1);
        adapter.shutdown();
        std::printf("[PASS] Keyboard mode controller count = 1\n");
    }

    // ── Test 9: getControllerCount when not initialized ───────────────────
    {
        XboxInputAdapter adapter;
        CHECK(adapter.getControllerCount() == 0);
        std::printf("[PASS] Uninitialized controller count = 0\n");
    }

    // ── Test 10: setDeadzone does not crash ───────────────────────────────
    {
        XboxInputAdapter adapter;
        adapter.initialize(InputBackend::Keyboard);
        DeadzoneConfig dz = {0.2f, 0.9f, true};
        adapter.setDeadzone(false, dz); // left stick
        adapter.setDeadzone(true, dz);  // right stick
        // Verify state still works after deadzone change
        auto state = adapter.getState();
        CHECK(state.connected);
        adapter.shutdown();
        std::printf("[PASS] setDeadzone safe\n");
    }

    // ── Test 11: setVibration returns false in keyboard mode ──────────────
    {
        XboxInputAdapter adapter;
        adapter.initialize(InputBackend::Keyboard);
        VibrationState vib = {0.5f, 0.5f, 0.0f, 0.0f};
        bool ok = adapter.setVibration(0, vib);
        CHECK(!ok); // keyboard has no vibration
        adapter.shutdown();
        std::printf("[PASS] Vibration rejected in keyboard mode\n");
    }

    // ── Test 12: Default keyboard mapping has valid VK codes ──────────────
    {
        auto map = getDefaultKeyboardMapping();
        CHECK(map.keyA != 0);
        CHECK(map.keyB != 0);
        CHECK(map.keyX != 0);
        CHECK(map.keyY != 0);
        CHECK(map.keyStart != 0);
        CHECK(map.keyBack != 0);
        CHECK(map.keyDPadUp != 0);
        CHECK(map.keyLStickUp != 0);
        CHECK(map.keyRStickUp != 0);
        // Verify some specific expected values
        CHECK(map.keyA == 'Z');
        CHECK(map.keyB == 'X');
        CHECK(map.keyLB == 'Q');
        CHECK(map.keyRB == 'E');
        std::printf("[PASS] Default keyboard mapping valid\n");
    }

    // ── Test 13: XboxButton enum values match XINPUT flags ────────────────
    {
        CHECK(static_cast<uint32_t>(XboxButton::DPadUp) == 0x0001);
        CHECK(static_cast<uint32_t>(XboxButton::A) == 0x1000);
        CHECK(static_cast<uint32_t>(XboxButton::B) == 0x2000);
        CHECK(static_cast<uint32_t>(XboxButton::X) == 0x4000);
        CHECK(static_cast<uint32_t>(XboxButton::Y) == 0x8000);
        CHECK(static_cast<uint32_t>(XboxButton::LeftShoulder) == 0x0100);
        CHECK(static_cast<uint32_t>(XboxButton::RightShoulder) == 0x0200);
        std::printf("[PASS] XboxButton enum values correct\n");
    }

    // ── Test 14: Re-initialization after shutdown works ───────────────────
    {
        XboxInputAdapter adapter;
        adapter.initialize(InputBackend::Keyboard);
        adapter.shutdown();
        bool ok = adapter.initialize(InputBackend::XInput);
        CHECK(ok);
        CHECK(std::string(adapter.getBackendName()) == "XInput");
        adapter.shutdown();
        std::printf("[PASS] Re-initialization after shutdown\n");
    }

    std::printf("\n=== ALL INPUT ADAPTER TESTS PASSED ===\n");
    return 0;
}