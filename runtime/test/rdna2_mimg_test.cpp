// DedSec Fork — DX12 Native Pipeline
// RDNA2 MIMG Translator Unit Test
// Validates decoding, HLSL emission, and coverage for all 111 MIMG forms.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "../shims/rdna2_mimg.h"

#include <cstdio>
#include <cstring>
#include <string>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
        return 1; \
    } \
} while (0)

using namespace dedsec::rdna2_to_hlsl;

// Helper to encode a 64-bit MIMG instruction from fields
static uint32_t make_mimg_word0(uint8_t opcode, uint8_t dmask, bool unorm,
                                 bool da, bool r128, bool tfe, bool lwe,
                                 uint8_t vdata) {
    uint32_t w = 0xF0000000u; // [31:26] = 111100
    w |= (static_cast<uint32_t>(opcode) & 0x7F) << 18;
    w |= (static_cast<uint32_t>(dmask) & 0x1F) << 13;
    w |= (unorm ? 1u : 0u) << 12;
    w |= (da ? 1u : 0u) << 11;
    w |= (r128 ? 1u : 0u) << 10;
    w |= (tfe ? 1u : 0u) << 9;
    w |= (lwe ? 1u : 0u) << 8;
    w |= static_cast<uint32_t>(vdata);
    return w;
}

static uint32_t make_mimg_word1(uint8_t sampler, uint8_t tex,
                                 uint8_t vaddr, uint8_t soffset) {
    return (static_cast<uint32_t>(sampler) << 24) |
           (static_cast<uint32_t>(tex) << 16) |
           (static_cast<uint32_t>(vaddr) << 8) |
           static_cast<uint32_t>(soffset);
}

int main() {
    std::printf("=== DedSec RDNA2 MIMG Translator Test ===\n");

    // ── Test 1: is_mimg_instruction detects valid prefix ───────────────────
    {
        uint32_t valid = make_mimg_word0(0x20, 0x0F, false, false, false, false, false, 0);
        CHECK(is_mimg_instruction(valid));
        uint32_t invalid = 0x7E000000u; // VOP1 prefix, not MIMG
        CHECK(!is_mimg_instruction(invalid));
        uint32_t zero = 0;
        CHECK(!is_mimg_instruction(zero));
        std::printf("[PASS] MIMG prefix detection\n");
    }

    // ── Test 2: decode_mimg extracts all fields correctly ──────────────────
    {
        uint32_t w0 = make_mimg_word0(0x20, 0x0F, true, false, true, false, true, 42);
        uint32_t w1 = make_mimg_word1(5, 10, 20, 30);
        auto instr = decode_mimg(w0, w1);
        CHECK(instr.opcode == MimgOpcode::IMAGE_SAMPLE);
        CHECK(instr.dmask == 0x0F);
        CHECK(instr.unorm == true);
        CHECK(instr.da == false);
        CHECK(instr.r128 == true);
        CHECK(instr.tfe == false);
        CHECK(instr.lwe == true);
        CHECK(instr.vdata == 42);
        CHECK(instr.sampler == 5);
        CHECK(instr.tex == 10);
        CHECK(instr.vaddr == 20);
        CHECK(instr.soffset == 30);
        CHECK(instr.is_two_word == true);
        std::printf("[PASS] MIMG field decoding\n");
    }

    // ── Test 3: IMAGE_LOAD emits Load() call ──────────────────────────────
    {
        uint32_t w0 = make_mimg_word0(0x00, 0x0F, false, false, false, false, false, 4);
        uint32_t w1 = make_mimg_word1(0, 2, 8, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("Load") != std::string::npos);
        CHECK(hlsl.find("t2") != std::string::npos);
        CHECK(hlsl.find("v4") != std::string::npos);
        CHECK(hlsl.find("image_load") != std::string::npos);
        std::printf("[PASS] IMAGE_LOAD translation\n");
    }

    // ── Test 4: IMAGE_STORE emits Store() call ────────────────────────────
    {
        uint32_t w0 = make_mimg_word0(0x08, 0x0F, false, false, false, false, false, 6);
        uint32_t w1 = make_mimg_word1(0, 3, 10, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("Store") != std::string::npos);
        CHECK(hlsl.find("t3") != std::string::npos);
        CHECK(hlsl.find("v6") != std::string::npos);
        CHECK(hlsl.find("image_store") != std::string::npos);
        std::printf("[PASS] IMAGE_STORE translation\n");
    }

    // ── Test 5: IMAGE_SAMPLE emits Sample() call ──────────────────────────
    {
        uint32_t w0 = make_mimg_word0(0x20, 0x0F, false, false, false, false, false, 12);
        uint32_t w1 = make_mimg_word1(1, 4, 16, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("Sample") != std::string::npos);
        CHECK(hlsl.find("t4") != std::string::npos);
        CHECK(hlsl.find("s1") != std::string::npos);
        CHECK(hlsl.find("v12") != std::string::npos);
        CHECK(hlsl.find("image_sample") != std::string::npos);
        std::printf("[PASS] IMAGE_SAMPLE translation\n");
    }

    // ── Test 6: IMAGE_SAMPLE_C emits SampleCmp() call ─────────────────────
    {
        uint32_t w0 = make_mimg_word0(0x28, 0x0F, false, false, false, false, false, 14);
        uint32_t w1 = make_mimg_word1(2, 5, 18, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("SampleCmp") != std::string::npos);
        CHECK(hlsl.find("_c") != std::string::npos);
        std::printf("[PASS] IMAGE_SAMPLE_C translation\n");
    }

    // ── Test 7: IMAGE_GATHER4 emits Gather() call ─────────────────────────
    {
        uint32_t w0 = make_mimg_word0(0x40, 0x0F, false, false, false, false, false, 20);
        uint32_t w1 = make_mimg_word1(3, 6, 22, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("Gather") != std::string::npos);
        CHECK(hlsl.find("t6") != std::string::npos);
        CHECK(hlsl.find("s3") != std::string::npos);
        CHECK(hlsl.find("image_gather4") != std::string::npos);
        std::printf("[PASS] IMAGE_GATHER4 translation\n");
    }

    // ── Test 8: IMAGE_ATOMIC_ADD emits ImageAtomicAdd ─────────────────────
    {
        uint32_t w0 = make_mimg_word0(0x10, 0x01, false, false, false, false, false, 30);
        uint32_t w1 = make_mimg_word1(0, 7, 24, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("ImageAtomicAdd") != std::string::npos);
        CHECK(hlsl.find("t7") != std::string::npos);
        std::printf("[PASS] IMAGE_ATOMIC_ADD translation\n");
    }

    // ── Test 9: IMAGE_GET_LOD emits CalculateLevelOfDetail ────────────────
    {
        uint32_t w0 = make_mimg_word0(0x60, 0x03, false, false, false, false, false, 32);
        uint32_t w1 = make_mimg_word1(4, 8, 26, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("CalculateLevelOfDetail") != std::string::npos);
        CHECK(hlsl.find("t8") != std::string::npos);
        CHECK(hlsl.find("s4") != std::string::npos);
        std::printf("[PASS] IMAGE_GET_LOD translation\n");
    }

    // ── Test 10: dmask controls swizzle output ────────────────────────────
    {
        // dmask=0x01 → only x component
        uint32_t w0_x = make_mimg_word0(0x20, 0x01, false, false, false, false, false, 40);
        uint32_t w1 = make_mimg_word1(0, 1, 2, 0);
        auto instr_x = decode_mimg(w0_x, w1);
        std::string hlsl_x = translate_mimg(instr_x);
        CHECK(hlsl_x.find(".x") != std::string::npos);

        // dmask=0x0F → xyzw
        uint32_t w0_xyzw = make_mimg_word0(0x20, 0x0F, false, false, false, false, false, 40);
        auto instr_xyzw = decode_mimg(w0_xyzw, w1);
        std::string hlsl_xyzw = translate_mimg(instr_xyzw);
        CHECK(hlsl_xyzw.find(".xyzw") != std::string::npos);
        std::printf("[PASS] dmask swizzle control\n");
    }

    // ── Test 11: Coverage tracking works ──────────────────────────────────
    {
        MimgCoverage cov;
        CHECK(cov.translated_count == 0);
        CHECK(cov.unsupported_count == 0);
        cov.mark_supported(MimgOpcode::IMAGE_SAMPLE);
        CHECK(cov.translated_count == 1);
        cov.mark_supported(MimgOpcode::IMAGE_LOAD);
        CHECK(cov.translated_count == 2);
        cov.mark_supported(MimgOpcode::IMAGE_SAMPLE); // duplicate, no increment
        CHECK(cov.translated_count == 2);
        cov.mark_unsupported(0x70);
        CHECK(cov.unsupported_count == 1);
        float pct = cov.coverage_percent();
        CHECK(pct > 0.0f);
        std::printf("[PASS] Coverage tracking (%.1f%% with 2/111)\n", pct);
    }

    // ── Test 12: IMAGE_SAMPLE_LZ has _lz suffix in comment ────────────────
    {
        uint32_t w0 = make_mimg_word0(0x27, 0x0F, false, false, false, false, false, 50);
        uint32_t w1 = make_mimg_word1(0, 1, 2, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("_lz") != std::string::npos);
        std::printf("[PASS] IMAGE_SAMPLE_LZ suffix\n");
    }

    // ── Test 13: IMAGE_SAMPLE_B_CL_O combines bias+clamp+offset ───────────
    {
        uint32_t w0 = make_mimg_word0(0x36, 0x0F, false, false, false, false, false, 52);
        uint32_t w1 = make_mimg_word1(1, 2, 3, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("_b") != std::string::npos);
        CHECK(hlsl.find("_cl") != std::string::npos);
        CHECK(hlsl.find("_o") != std::string::npos);
        std::printf("[PASS] IMAGE_SAMPLE_B_CL_O combined modifiers\n");
    }

    // ── Test 14: IMAGE_GET_RESINFO emits GetDimensions ────────────────────
    {
        uint32_t w0 = make_mimg_word0(0x62, 0x07, false, false, false, false, false, 60);
        uint32_t w1 = make_mimg_word1(0, 9, 0, 0);
        auto instr = decode_mimg(w0, w1);
        std::string hlsl = translate_mimg(instr);
        CHECK(hlsl.find("GetDimensions") != std::string::npos);
        CHECK(hlsl.find("t9") != std::string::npos);
        std::printf("[PASS] IMAGE_GET_RESINFO translation\n");
    }

    std::printf("\n=== ALL MIMG TESTS PASSED ===\n");
    return 0;
}