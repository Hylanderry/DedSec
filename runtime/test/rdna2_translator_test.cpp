// DedSec Fork — DX12 Native Pipeline
// RDNA2 → HLSL SM 6.6 Translator Unit Test
// Validates instruction decoding, HLSL emission, and coverage tracking.
// No GPU or game files required.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "../shims/rdna2_to_hlsl.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
        return 1; \
    } \
} while (0)

using namespace dedsec::rdna2_to_hlsl;

// Helper: encode a VOP1 instruction word
// VOP1 encoding: [31:25] = 0x7E, [24:9] = opcode(8 bits padded), [8:0] = vdst/src
static uint32_t make_vop1(uint32_t opcode, uint32_t vdst, uint32_t src0) {
    // RDNA2 VOP1 encoding matching translator decode:
    // [31:25]=0x7E, [24:17]=opcode(8), [16:9]=vdst(8), [8:0]=src0(8)
    return 0x7E000000u | ((opcode & 0xFF) << 17) | ((vdst & 0xFF) << 9) | (src0 & 0xFF);
}

// Helper: encode a VOP2 instruction word
// RDNA2 VOP2: bit31=0, bit30=1, [29:23]=opcode(7), [22:16]=vdst(7→8), [15:8]=src0(8), [7:0]=src1(8)
// Must match translator decode: opcode=(word>>23)&0x7F, vdst=(word>>16)&0xFF, src0=(word>>8)&0xFF, src1=(word>>0)&0xFF
static uint32_t make_vop2(uint32_t opcode, uint32_t vdst, uint32_t src0, uint32_t src1) {
    return 0x40000000u | ((opcode & 0x7F) << 23) | ((vdst & 0xFF) << 16) | ((src0 & 0xFF) << 8) | (src1 & 0xFF);
}

int main() {
    std::printf("=== DedSec RDNA2→HLSL Translator Test ===\n");

    // ── Test 1: Empty/null bytecode returns error ─────────────────────────────
    {
        auto result = translate_shader(nullptr, 0, ShaderStage::Compute);
        CHECK(!result.success);
        CHECK(result.instruction_count == 0);
        std::printf("[PASS] Null bytecode rejected\n");
    }

    // ── Test 2: Single VOP1 v_mov_b32 translates correctly ────────────────────
    {
        // v_mov_b32 opcode=0x001, vdst=v0, src0=v1
        uint32_t instr = make_vop1(0x001, 0, 1);
        auto result = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Compute);
        CHECK(result.success);
        CHECK(result.instruction_count == 1);
        CHECK(result.hlsl_source.find("v0") != std::string::npos);
        CHECK(result.hlsl_source.find("v1") != std::string::npos);
        CHECK(result.hlsl_source.find("mov") != std::string::npos || result.hlsl_source.find("v0 = v1") != std::string::npos);
        std::printf("[PASS] VOP1 v_mov_b32 translated\n");
    }

    // ── Test 3: VOP1 v_abs_f32 maps to abs() ─────────────────────────────────
    {
        uint32_t instr = make_vop1(0x035, 2, 3); // v_abs_f32 v2, v3
        auto result = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Pixel);
        CHECK(result.success);
        CHECK(result.hlsl_source.find("abs") != std::string::npos);
        std::printf("[PASS] VOP1 v_abs_f32 → abs()\n");
    }

    // ── Test 4: VOP1 v_sin_f32 maps to sin() ─────────────────────────────────
    {
        uint32_t instr = make_vop1(0x025, 4, 5); // v_sin_f32 v4, v5
        auto result = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Vertex);
        CHECK(result.success);
        CHECK(result.hlsl_source.find("sin") != std::string::npos);
        std::printf("[PASS] VOP1 v_sin_f32 → sin()\n");
    }

    // ── Test 5: VOP2 v_add_f32 translates with + operator ────────────────────
    {
        uint32_t instr = make_vop2(0x001, 0, 1, 2); // v_add_f32 v0, v1, v2
        std::printf("[DEBUG] VOP2 instr word: 0x%08X\n", instr);
        auto result = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Compute);
        std::printf("[DEBUG] success=%d instr_count=%u unsupported=%u\n", result.success, result.instruction_count, result.unsupported_count);
        std::printf("[DEBUG] HLSL output:\n%s\n", result.hlsl_source.c_str());
        CHECK(result.success);
        CHECK(result.hlsl_source.find("+") != std::string::npos);
        std::printf("[PASS] VOP2 v_add_f32 → +\n");
    }

    // ── Test 6: VOP2 v_mul_f32 translates with * operator ────────────────────
    {
        uint32_t instr = make_vop2(0x017, 3, 4, 5); // v_mul_f32 v3, v4, v5
        auto result = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Compute);
        CHECK(result.success);
        CHECK(result.hlsl_source.find("*") != std::string::npos);
        std::printf("[PASS] VOP2 v_mul_f32 → *\n");
    }

    // ── Test 7: Multiple instructions in sequence ─────────────────────────────
    {
        std::vector<uint32_t> program = {
            make_vop1(0x001, 0, 1),  // v_mov_b32 v0, v1
            make_vop1(0x035, 2, 0),  // v_abs_f32 v2, v0
            make_vop2(0x001, 3, 2, 1), // v_add_f32 v3, v2, v1
        };
        auto result = translate_shader(
            reinterpret_cast<const uint8_t*>(program.data()),
            program.size() * sizeof(uint32_t),
            ShaderStage::Compute);
        CHECK(result.success);
        CHECK(result.instruction_count == 3);
        CHECK(result.unsupported_count <= 1); // at most 1 unsupported
        std::printf("[PASS] Multi-instruction sequence (%u instr, %u unsupported)\n",
                    result.instruction_count, result.unsupported_count);
    }

    // ── Test 8: Shader stage headers are correct ──────────────────────────────
    {
        uint32_t instr = make_vop1(0x001, 0, 0);

        auto vs = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Vertex);
        CHECK(vs.hlsl_source.find("vertex") != std::string::npos);

        auto ps = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Pixel);
        CHECK(ps.hlsl_source.find("pixel") != std::string::npos);

        auto cs = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Compute);
        CHECK(cs.hlsl_source.find("compute") != std::string::npos);
        CHECK(cs.hlsl_source.find("numthreads") != std::string::npos);

        std::printf("[PASS] Shader stage headers correct\n");
    }

    // ── Test 9: Coverage table initializes with correct totals ────────────────
    {
        Translator t;
        auto cov = t.get_coverage();
        CHECK(cov[static_cast<size_t>(IsaCategory::VOP3)].total == 425);
        CHECK(cov[static_cast<size_t>(IsaCategory::VOP1)].total == 81);
        CHECK(cov[static_cast<size_t>(IsaCategory::VOP2)].total == 47);
        CHECK(cov[static_cast<size_t>(IsaCategory::SDWA)].total == 220);
        CHECK(cov[static_cast<size_t>(IsaCategory::MIMG)].total == 111);
        std::printf("[PASS] Coverage table initialized correctly\n");
    }

    // ── Test 10: is_form_supported works for known opcodes ────────────────────
    {
        Translator t;
        CHECK(t.is_form_supported(0x001, IsaCategory::VOP1));  // v_mov_b32
        CHECK(t.is_form_supported(0x025, IsaCategory::VOP1));  // v_sin_f32
        CHECK(t.is_form_supported(0x035, IsaCategory::VOP1));  // v_abs_f32
        CHECK(!t.is_form_supported(0x0FF, IsaCategory::VOP1)); // nonexistent
        CHECK(t.is_form_supported(0x001, IsaCategory::VOP2));  // v_add_f32
        CHECK(t.is_form_supported(0x017, IsaCategory::VOP2));  // v_mul_f32
        CHECK(!t.is_form_supported(0x001, IsaCategory::VOP3)); // VOP3 not implemented yet
        std::printf("[PASS] is_form_supported correct\n");
    }

    // ── Test 11: Generated HLSL contains closing brace ────────────────────────
    {
        uint32_t instr = make_vop1(0x001, 0, 0);
        auto result = translate_shader(reinterpret_cast<const uint8_t*>(&instr), sizeof(instr), ShaderStage::Compute);
        CHECK(result.hlsl_source.find("}") != std::string::npos);
        CHECK(result.hlsl_source.find("DedSec Fork") != std::string::npos);
        std::printf("[PASS] HLSL output well-formed\n");
    }

    std::printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}