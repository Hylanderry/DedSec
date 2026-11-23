// DedSec CommandList Draw/Dispatch Test — validates the new draw pipeline commands
// in the CommandList IR: SetPipelineState, SetRootSignature, SetVertexBuffer,
// SetIndexBuffer, SetViewports, SetScissors, SetRenderTargets, DrawInstanced,
// DrawIndexedInstanced, Dispatch, SetRootConstants, SetRootDescriptorTable.
//
// This test records a full draw sequence into a CommandList and verifies:
// 1. All commands are accepted (return true)
// 2. Invalid inputs are rejected (return false)
// 3. The recorded command count matches expectations
// 4. Commands survive close/reset cycle
//
// No GPU required — pure IR validation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "../src/command_list.h"

#include <cassert>
#include <cstdio>
#include <variant>

using namespace dedsec;

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool condition, const char* name) {
    if (condition) {
        ++g_pass;
        std::printf("  [PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", name);
    }
}

// Dummy resource IDs for testing (non-zero = valid)
constexpr ResourceId kPso = 0x1001;
constexpr ResourceId kRootSig = 0x2001;
constexpr ResourceId kResourceA = 0x3001;
constexpr ResourceId kResourceB = 0x3002;
constexpr GpuVirtualAddress kVbAddr = 0x100000;
constexpr GpuVirtualAddress kIbAddr = 0x200000;
constexpr GpuVirtualAddress kTableAddr = 0x300000;

} // namespace

int main() {
    std::printf("[CMDLIST-DRAW] Testing Draw/Dispatch/PSO commands...\n\n");

    CommandList cl;

    // --- Valid commands ---
    std::printf("--- Valid Commands ---\n");

    check(cl.set_pipeline_state(kPso), "set_pipeline_state(valid)");
    check(cl.set_root_signature(kRootSig), "set_root_signature(valid)");
    check(cl.set_vertex_buffer(0, kVbAddr, 32, 1024), "set_vertex_buffer(slot=0)");
    check(cl.set_index_buffer(kIbAddr, IndexFormat::uint16, 512), "set_index_buffer(uint16)");
    check(cl.set_index_buffer(kIbAddr, IndexFormat::uint32, 1024), "set_index_buffer(uint32)");

    Viewport vp{0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f};
    Viewport vp_arr[] = {vp};
    check(cl.set_viewports(vp_arr), "set_viewports(1 viewport)");

    Rect scissor{0, 0, 1280, 720};
    Rect sc_arr[] = {scissor};
    check(cl.set_scissors(sc_arr), "set_scissors(1 rect)");

    TargetView rtv{kResourceA, 0, 0, 1};  // mip=0, first_layer=0, layer_count=1
    TargetView rtv_arr[] = {rtv};
    check(cl.set_render_targets(rtv_arr, false), "set_render_targets(1 RTV)");

    TargetView dsv{kResourceB, 0, 0, 1};  // mip=0, first_layer=0, layer_count=1
    check(cl.set_render_targets(rtv_arr, false, dsv), "set_render_targets(1 RTV + DSV)");

    check(cl.draw_instanced(3, 1), "draw_instanced(3 verts, 1 instance)");
    check(cl.draw_instanced(36, 100, 0, 0), "draw_instanced(36 verts, 100 instances)");
    check(cl.draw_indexed_instanced(6, 1), "draw_indexed_instanced(6 indices, 1 instance)");
    check(cl.draw_indexed_instanced(1024, 64, 0, -128, 0), "draw_indexed_instanced(base_vertex=-128)");
    check(cl.dispatch(8, 8, 1), "dispatch(8x8x1)");
    check(cl.dispatch(256, 1, 1), "dispatch(256x1x1)");

    std::uint32_t constants[] = {0xDEAD, 0xBEEF, 42};
    check(cl.set_root_constants(0, constants), "set_root_constants(3 values)");
    check(cl.set_root_descriptor_table(1, kTableAddr), "set_root_descriptor_table(slot=1)");

    // --- Invalid commands (must be rejected) ---
    std::printf("\n--- Invalid Commands (must reject) ---\n");

    check(!cl.set_pipeline_state(0), "set_pipeline_state(null) rejected");
    check(!cl.set_root_signature(0), "set_root_signature(null) rejected");
    check(!cl.set_vertex_buffer(0, 0, 32, 1024), "set_vertex_buffer(null addr) rejected");
    check(!cl.set_vertex_buffer(0, kVbAddr, 0, 1024), "set_vertex_buffer(zero stride) rejected");
    check(!cl.set_vertex_buffer(0, kVbAddr, 32, 0), "set_vertex_buffer(zero size) rejected");
    check(!cl.set_index_buffer(0, IndexFormat::uint16, 512), "set_index_buffer(null addr) rejected");
    check(!cl.set_index_buffer(kIbAddr, IndexFormat::uint16, 0), "set_index_buffer(zero size) rejected");

    Viewport bad_vp{0.0f, 0.0f, 0.0f, 720.0f, 0.0f, 1.0f}; // zero width
    Viewport bad_vp_arr[] = {bad_vp};
    check(!cl.set_viewports(bad_vp_arr), "set_viewports(zero width) rejected");

    Viewport bad_depth{0.0f, 0.0f, 100.0f, 100.0f, 0.5f, 0.3f}; // min > max
    Viewport bad_depth_arr[] = {bad_depth};
    check(!cl.set_viewports(bad_depth_arr), "set_viewports(min_depth > max_depth) rejected");

    Rect bad_scissor{100, 100, 50, 200}; // right < left
    Rect bad_sc_arr[] = {bad_scissor};
    check(!cl.set_scissors(bad_sc_arr), "set_scissors(right < left) rejected");

    check(!cl.draw_instanced(0, 1), "draw_instanced(zero verts) rejected");
    check(!cl.draw_instanced(3, 0), "draw_instanced(zero instances) rejected");
    check(!cl.draw_indexed_instanced(0, 1), "draw_indexed_instanced(zero indices) rejected");
    check(!cl.dispatch(0, 1, 1), "dispatch(zero x) rejected");
    check(!cl.dispatch(1, 0, 1), "dispatch(zero y) rejected");
    check(!cl.dispatch(1, 1, 0), "dispatch(zero z) rejected");

    check(!cl.set_root_constants(0, {}), "set_root_constants(empty) rejected");
    check(!cl.set_root_descriptor_table(0, 0), "set_root_descriptor_table(null addr) rejected");

    // --- Command count verification ---
    std::printf("\n--- Command Count ---\n");
    // Count only valid commands that were accepted
    // Valid: set_pso(1) + set_rootsig(1) + set_vb(1) + set_ib(2) + set_vp(1) + set_sc(1) +
    //        set_rt(2) + draw_inst(2) + draw_idx(2) + dispatch(2) + root_const(1) + root_table(1) = 17
    const auto cmds = cl.commands();
    std::printf("  Recorded commands: %zu\n", cmds.size());
    check(cmds.size() == 17, "exactly 17 commands recorded");

    // --- Variant type checks ---
    std::printf("\n--- Variant Type Checks ---\n");
    bool has_draw = false, has_dispatch = false, has_pso = false;
    for (const auto& cmd : cmds) {
        if (std::holds_alternative<DrawInstanced>(cmd)) has_draw = true;
        if (std::holds_alternative<Dispatch>(cmd)) has_dispatch = true;
        if (std::holds_alternative<SetPipelineState>(cmd)) has_pso = true;
    }
    check(has_draw, "DrawInstanced found in commands");
    check(has_dispatch, "Dispatch found in commands");
    check(has_pso, "SetPipelineState found in commands");

    // --- Close/Reset cycle ---
    std::printf("\n--- Close/Reset Cycle ---\n");
    check(cl.close(), "close() succeeds");
    check(cl.closed(), "closed() returns true after close");
    check(!cl.draw_instanced(3, 1), "draw rejected when closed");
    check(!cl.dispatch(1, 1, 1), "dispatch rejected when closed");
    check(cl.reset(), "reset() succeeds");
    check(!cl.closed(), "closed() returns false after reset");
    check(cl.draw_instanced(3, 1), "draw accepted after reset");

    // --- Summary ---
    std::printf("\n========================================\n");
    std::printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    std::printf("========================================\n");

    return g_fail > 0 ? 1 : 0;
}