// DedSec Replay Engine Test — IR recording, stats tracking, error recovery
// Tests the replay engine's command list management and statistics without
// requiring a real D3D12 device (portable tests).
// No game data.

#include "../src/replay_engine.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace dedsec;

static int g_tests = 0;
static int g_passed = 0;

#define CHECK(cond)                                                            \
    do {                                                                        \
        ++g_tests;                                                              \
        if (cond) {                                                             \
            ++g_passed;                                                         \
        } else {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                       \
    } while (0)

// --- Construction / lifecycle ---

static void test_construction() {
    ReplayEngine engine;
    CHECK(engine.command_list_count() == 0);
    auto s = engine.stats();
    CHECK(s.commands_replayed == 0);
    CHECK(s.draw_calls == 0);
}

static void test_add_and_clear() {
    ReplayEngine engine;

    ReplayCommandList rcl;
    rcl.ir.draw_instanced(100, 1);
    rcl.ir.close();
    rcl.id = 1;
    rcl.label = "test_cl";

    auto idx = engine.add_command_list(std::move(rcl));
    CHECK(idx == 0);
    CHECK(engine.command_list_count() == 1);

    ReplayCommandList rcl2;
    rcl2.ir.dispatch(4, 4, 1);
    rcl2.ir.close();
    rcl2.id = 2;

    auto idx2 = engine.add_command_list(std::move(rcl2));
    CHECK(idx2 == 1);
    CHECK(engine.command_list_count() == 2);

    engine.clear_command_lists();
    CHECK(engine.command_list_count() == 0);
}

// --- Error callback ---

static void test_error_callback() {
    ReplayEngine engine;

    std::vector<std::string> errors;
    engine.set_error_callback([&errors](int severity, const char* cmd, const char* msg) {
        errors.push_back(std::to_string(severity) + ":" + cmd + ":" + msg);
    });

    // Without a device, replay should fail gracefully
    // (no crash, error callback invoked if continue_on_error is true)
    ReplayCommandList rcl;
    rcl.ir.draw_instanced(10, 1);
    rcl.ir.close();
    engine.add_command_list(std::move(rcl));

    // replay without init → should return false but not crash
    bool result = engine.replay(0, nullptr);
    CHECK(!result);
}

// --- Stats reset ---

static void test_stats_reset() {
    ReplayEngine engine;
    auto s = engine.stats();
    CHECK(s.commands_replayed == 0);

    engine.reset_stats();
    s = engine.stats();
    CHECK(s.draw_calls == 0);
    CHECK(s.last_frame_ms == 0.0);
}

// --- Config defaults ---

static void test_config_defaults() {
    ReplayConfig config;
    CHECK(config.worker_threads == 0); // auto-detect
    CHECK(config.max_batch_size == 16);
    CHECK(config.continue_on_error == true);
    CHECK(config.log_failures == true);
}

// --- Resource resolvers ---

static void test_resource_resolvers() {
    ReplayEngine engine;

    bool resolver_called = false;
    engine.set_resource_resolver([&resolver_called](ResourceId) -> void* {
        resolver_called = true;
        return nullptr;
    });

    engine.set_pso_resolver([](ResourceId) -> void* { return nullptr; });
    engine.set_root_sig_resolver([](ResourceId) -> void* { return nullptr; });
    engine.set_heap_resolver([](ResourceId) -> void* { return nullptr; });

    // Resolvers are stored; they'd be called during replay with a real device.
    // Here we just verify no crash on set.
    CHECK(!resolver_called); // not called yet
}

// --- Multiple command lists ---

static void test_multiple_command_lists() {
    ReplayEngine engine;

    for (int i = 0; i < 10; ++i) {
        ReplayCommandList rcl;
        rcl.ir.draw_instanced(static_cast<std::uint32_t>(i + 1), 1);
        rcl.ir.close();
        rcl.id = static_cast<std::uint64_t>(i);
        engine.add_command_list(std::move(rcl));
    }

    CHECK(engine.command_list_count() == 10);
}

// --- Compute-only flag ---

static void test_compute_only_flag() {
    ReplayEngine engine;

    ReplayCommandList rcl;
    rcl.ir.dispatch(8, 8, 1);
    rcl.ir.close();
    rcl.compute_only = true;
    rcl.label = "compute_pass";

    engine.add_command_list(std::move(rcl));
    CHECK(engine.command_list_count() == 1);
}

int main() {
    test_construction();
    test_add_and_clear();
    test_error_callback();
    test_stats_reset();
    test_config_defaults();
    test_resource_resolvers();
    test_multiple_command_lists();
    test_compute_only_flag();

    std::printf("replay_engine_test: %d/%d passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}