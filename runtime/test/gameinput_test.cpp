// DedSec Fork — GameInput / KBM Mapping Unit Tests (Sprint 4)
// Tests the lock-free ring buffer, event push/pop, and KBM mapping logic.
// No game data required; runs standalone on Windows.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "gameinput.h"
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace dedsec::input;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct TestReg_##name { TestReg_##name() { test_##name(); } }; \
    static TestReg_##name s_reg_##name; \
    static void test_##name()

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_testsFailed++; \
        return; \
    } \
} while(0)

#define PASS() do { g_testsPassed++; } while(0)

// ─── Ring Buffer Tests ────────────────────────────────────────────────────────

TEST(ring_buffer_empty_on_creation) {
    InputRingBuffer<16> rb;
    CHECK(rb.empty());
    CHECK(rb.capacity() == 16);

    InputEvent ev;
    CHECK(!rb.pop(ev));
    PASS();
}

TEST(ring_buffer_push_pop_single) {
    InputRingBuffer<16> rb;

    InputEvent ev{};
    ev.type = InputEvent::Type::Button;
    ev.deviceId = 42;
    ev.timestampQpc = 12345;
    ev.data.button.button = 7;
    ev.data.button.pressed = true;

    CHECK(rb.push(ev));
    CHECK(!rb.empty());

    InputEvent out{};
    CHECK(rb.pop(out));
    CHECK(out.type == InputEvent::Type::Button);
    CHECK(out.deviceId == 42);
    CHECK(out.timestampQpc == 12345);
    CHECK(out.data.button.button == 7);
    CHECK(out.data.button.pressed == true);
    CHECK(rb.empty());
    PASS();
}

TEST(ring_buffer_fill_to_capacity) {
    InputRingBuffer<8> rb; // capacity 8, usable slots = 7 (one slot wasted for full detection)

    InputEvent ev{};
    ev.type = InputEvent::Type::Axis;

    // Fill all usable slots
    size_t pushed = 0;
    for (size_t i = 0; i < 7; i++) {
        ev.data.axis.x = static_cast<float>(i);
        if (rb.push(ev)) pushed++;
    }
    CHECK(pushed == 7);

    // Next push should fail (buffer full)
    CHECK(!rb.push(ev));

    // Drain all
    InputEvent buf[8];
    size_t drained = rb.drain(buf, 8);
    CHECK(drained == 7);
    CHECK(buf[0].data.axis.x == 0.0f);
    CHECK(buf[6].data.axis.x == 6.0f);
    CHECK(rb.empty());
    PASS();
}

TEST(ring_buffer_wrap_around) {
    InputRingBuffer<4> rb; // 3 usable slots

    InputEvent ev{};
    ev.type = InputEvent::Type::MouseMove;

    // Push 3, pop 3, push 3 again to test wrap-around
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 3; i++) {
            ev.data.mouse.dx = round * 10 + i;
            CHECK(rb.push(ev));
        }
        CHECK(!rb.push(ev)); // full

        for (int i = 0; i < 3; i++) {
            InputEvent out;
            CHECK(rb.pop(out));
            CHECK(out.data.mouse.dx == round * 10 + i);
        }
        CHECK(rb.empty());
    }
    PASS();
}

TEST(ring_buffer_drain_partial) {
    InputRingBuffer<16> rb;

    InputEvent ev{};
    ev.type = InputEvent::Type::Button;
    for (int i = 0; i < 10; i++) {
        ev.data.button.button = static_cast<uint32_t>(i);
        rb.push(ev);
    }

    InputEvent buf[4];
    size_t drained = rb.drain(buf, 4);
    CHECK(drained == 4);
    CHECK(buf[0].data.button.button == 0);
    CHECK(buf[3].data.button.button == 3);

    // Remaining 6 still in buffer
    drained = rb.drain(buf, 4);
    CHECK(drained == 4);
    CHECK(buf[0].data.button.button == 4);

    drained = rb.drain(buf, 4);
    CHECK(drained == 2);
    CHECK(rb.empty());
    PASS();
}

TEST(input_event_trivially_copyable) {
    CHECK(std::is_trivially_copyable_v<InputEvent>);
    CHECK(sizeof(InputEvent) <= 32); // Compact fixed-size event
    PASS();
}

TEST(kbm_mapping_defaults) {
    KbmToControllerMap map;
    CHECK(map.mouseSensitivityX == 1.0f);
    CHECK(map.mouseSensitivityY == 1.0f);
    CHECK(map.keyForward == 'W');
    CHECK(map.keyBack == 'S');
    CHECK(map.keyLeft == 'A');
    CHECK(map.keyRight == 'D');
    CHECK(map.mouseLookEnabled == true);
    PASS();
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== DedSec GameInput Unit Tests ===\n");
    std::printf("Passed: %d | Failed: %d\n", g_testsPassed, g_testsFailed);
    return g_testsFailed > 0 ? 1 : 0;
}