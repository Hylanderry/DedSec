// DedSec Command List IR Full Test — expanded commands, ring buffer, serialization
// Tests all Sprint 2 additions: ClearUAV, SetDescriptorHeaps, SetComputeRootSignature,
// ExecuteIndirect, VRS, sample positions, blend factor, stencil ref, stream output,
// predication, ring allocator, and serialize/deserialize round-trip.
// No game data; no Windows SDK dependency (IR is portable).

#include "../src/command_list.h"

#include <cassert>
#include <cstdio>
#include <cstring>
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

// --- New command recording tests ---

static void test_clear_uav_float() {
    CommandList cl;
    TargetView tv{1, 0, 0, 1};
    ClearColor color{0.0f, 0.5f, 1.0f, 0.25f};
    CHECK(cl.clear_uav_float(tv, color));
    CHECK(cl.commands().size() == 1);
    auto& cmd = cl.commands()[0];
    CHECK(std::holds_alternative<ClearUav>(cmd));
    auto& cu = std::get<ClearUav>(cmd);
    CHECK(cu.use_float == true);
    CHECK(cu.float_values[1] == 0.5f);
}

static void test_clear_uav_uint() {
    CommandList cl;
    TargetView tv{2, 0, 0, 1};
    std::array<std::uint32_t, 4> vals{1, 2, 3, 4};
    CHECK(cl.clear_uav_uint(tv, vals));
    auto& cmd = cl.commands()[0];
    CHECK(std::holds_alternative<ClearUav>(cmd));
    auto& cu = std::get<ClearUav>(cmd);
    CHECK(cu.use_float == false);
    CHECK(cu.uint_values[3] == 4);
}

static void test_set_descriptor_heaps() {
    CommandList cl;
    ResourceId heaps[] = {10, 20};
    CHECK(cl.set_descriptor_heaps(heaps));
    auto& cmd = cl.commands()[0];
    CHECK(std::holds_alternative<SetDescriptorHeaps>(cmd));
    auto& sdh = std::get<SetDescriptorHeaps>(cmd);
    CHECK(sdh.heap_count == 2);
    CHECK(sdh.heaps[0] == 10);
    CHECK(sdh.heaps[1] == 20);
}

static void test_set_descriptor_heaps_too_many() {
    CommandList cl;
    ResourceId heaps[] = {1, 2, 3};
    CHECK(!cl.set_descriptor_heaps(heaps)); // max 2
}

static void test_set_compute_root_signature() {
    CommandList cl;
    CHECK(cl.set_compute_root_signature(42));
    auto& cmd = cl.commands()[0];
    CHECK(std::holds_alternative<SetComputeRootSignature>(cmd));
    CHECK(std::get<SetComputeRootSignature>(cmd).root_signature == 42);
}

static void test_execute_indirect() {
    CommandList cl;
    CHECK(cl.execute_indirect(IndirectCommandType::draw_indexed, 100, 0, 64, 200, 8));
    auto& cmd = cl.commands()[0];
    CHECK(std::holds_alternative<ExecuteIndirect>(cmd));
    auto& ei = std::get<ExecuteIndirect>(cmd);
    CHECK(ei.type == IndirectCommandType::draw_indexed);
    CHECK(ei.max_command_count == 64);
    CHECK(ei.count_buffer == 200);
}

static void test_blend_factor_and_stencil_ref() {
    CommandList cl;
    CHECK(cl.set_blend_factor(0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(cl.set_stencil_ref(7));
    CHECK(cl.commands().size() == 2);
    auto& bf = std::get<SetBlendFactor>(cl.commands()[0]);
    CHECK(bf.factor[2] == 0.3f);
    auto& sr = std::get<SetStencilRef>(cl.commands()[1]);
    CHECK(sr.stencil_ref == 7);
}

static void test_sample_positions() {
    CommandList cl;
    SamplePosition positions[] = {{0, 0}, {4, 4}};
    CHECK(cl.set_sample_positions(2, 1, positions));
    auto& sp = std::get<SetSamplePositions>(cl.commands()[0]);
    CHECK(sp.sample_count == 2);
    CHECK(sp.positions.size() == 2);
}

static void test_shading_rate() {
    CommandList cl;
    CHECK(cl.set_shading_rate(ShadingRate::rate_2x2, 1, 2));
    auto& sr = std::get<SetShadingRate>(cl.commands()[0]);
    CHECK(sr.base_rate == ShadingRate::rate_2x2);
    CHECK(sr.combiner_ops[0] == 1);
}

static void test_stream_output_targets() {
    CommandList cl;
    SoTarget targets[] = {{10, 0, 1024}, {20, 512, 2048}};
    CHECK(cl.set_stream_output_targets(targets, true));
    auto& so = std::get<SetStreamOutputTargets>(cl.commands()[0]);
    CHECK(so.targets.size() == 2);
    CHECK(so.append == true);
}

static void test_predication() {
    CommandList cl;
    CHECK(cl.set_predication(50, 16, true));
    auto& p = std::get<SetPredication>(cl.commands()[0]);
    CHECK(p.buffer == 50);
    CHECK(p.condition_equal_zero == true);
}

// --- Ring Buffer Allocator tests ---

static void test_ring_allocator_basic() {
    CommandList::RingAllocator ring(8);
    CHECK(ring.capacity() == 8);
    CHECK(ring.size() == 0);
    CHECK(!ring.full());

    for (int i = 0; i < 8; ++i) {
        auto* slot = ring.allocate();
        CHECK(slot != nullptr);
    }
    CHECK(ring.full());
    CHECK(ring.size() == 8);
    CHECK(ring.allocate() == nullptr); // full

    ring.reset();
    CHECK(ring.size() == 0);
    CHECK(!ring.full());
}

static void test_ring_allocator_move() {
    CommandList::RingAllocator ring(4);
    ring.allocate();
    ring.allocate();
    CHECK(ring.size() == 2);

    CommandList::RingAllocator ring2(std::move(ring));
    CHECK(ring2.size() == 2);
    CHECK(ring2.capacity() == 4);
    CHECK(ring.capacity() == 0); // moved-from
}

// --- Serialization round-trip tests ---

static void test_serialize_empty() {
    CommandList cl;
    cl.close();
    auto data = cl.serialize();
    CHECK(!data.empty());

    CommandList cl2;
    CHECK(cl2.deserialize(data));
    CHECK(cl2.commands().empty());
}

static void test_serialize_round_trip() {
    CommandList cl;

    // Record a variety of commands
    TransitionBarrier tb{1, 0, d3d12_state::common, d3d12_state::render_target};
    cl.transition(tb);

    cl.draw_instanced(100, 1, 0, 0);
    cl.draw_indexed_instanced(200, 2, 0, -10, 0);
    cl.dispatch(8, 8, 1);

    cl.set_pipeline_state(42);
    cl.set_root_signature(99);
    cl.set_compute_root_signature(101);

    Viewport vp{0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f};
    cl.set_viewports({vp});

    Rect scissor{0, 0, 1920, 1080};
    cl.set_scissors({scissor});

    cl.set_vertex_buffer(0, 0xDEAD0000, 32, 4096);
    cl.set_index_buffer(0xBEEF0000, IndexFormat::uint32, 2048);

    TargetView tv{5, 0, 0, 1};
    ClearColor cc{0.0f, 0.0f, 0.0f, 1.0f};
    cl.clear_render_target(tv, cc);
    cl.clear_depth_stencil(tv, clear_depth | clear_stencil, 1.0f, 0);
    cl.clear_uav_float(tv, cc);

    cl.set_blend_factor(1.0f, 0.5f, 0.0f, 1.0f);
    cl.set_stencil_ref(128);

    cl.set_shading_rate(ShadingRate::rate_1x1, 0, 0);

    CopyBuffer cb{10, 0, 20, 256, 512};
    cl.copy_buffer(cb);

    cl.close();

    auto data = cl.serialize();
    CHECK(!data.empty());

    CommandList cl2;
    CHECK(cl2.deserialize(data));
    CHECK(cl2.commands().size() == cl.commands().size());

    // Verify specific commands survived the round-trip
    CHECK(std::holds_alternative<TransitionBarrier>(cl2.commands()[0]));
    CHECK(std::holds_alternative<DrawInstanced>(cl2.commands()[1]));
    CHECK(std::holds_alternative<DrawIndexedInstanced>(cl2.commands()[2]));
    CHECK(std::holds_alternative<Dispatch>(cl2.commands()[3]));
    CHECK(std::holds_alternative<SetPipelineState>(cl2.commands()[4]));
    CHECK(std::holds_alternative<SetRootSignature>(cl2.commands()[5]));
    CHECK(std::holds_alternative<SetComputeRootSignature>(cl2.commands()[6]));

    auto& di = std::get<DrawIndexedInstanced>(cl2.commands()[2]);
    CHECK(di.index_count == 200);
    CHECK(di.instance_count == 2);
    CHECK(di.base_vertex == -10);

    auto& csp = std::get<SetComputeRootSignature>(cl2.commands()[6]);
    CHECK(csp.root_signature == 101);
}

static void test_deserialize_corrupt() {
    CommandList cl;
    std::vector<std::uint8_t> garbage = {0xFF, 0xFE, 0xFD};
    CHECK(!cl.deserialize(garbage));
}

static void test_deserialize_on_closed() {
    CommandList cl;
    cl.close();
    std::vector<std::uint8_t> data = {0, 0, 0, 0}; // count = 0
    CHECK(!cl.deserialize(data)); // closed list rejects deserialize
}

int main() {
    test_clear_uav_float();
    test_clear_uav_uint();
    test_set_descriptor_heaps();
    test_set_descriptor_heaps_too_many();
    test_set_compute_root_signature();
    test_execute_indirect();
    test_blend_factor_and_stencil_ref();
    test_sample_positions();
    test_shading_rate();
    test_stream_output_targets();
    test_predication();
    test_ring_allocator_basic();
    test_ring_allocator_move();
    test_serialize_empty();
    test_serialize_round_trip();
    test_deserialize_corrupt();
    test_deserialize_on_closed();

    std::printf("command_list_full_test: %d/%d passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}