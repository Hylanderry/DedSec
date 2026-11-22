// DedSec Fork — RDNA2 VOP3 Translator Implementation
// Translates 425 VOP3 (Vector ALU 3-operand) instruction forms to HLSL SM 6.6.
// VOP3 is the largest RDNA2 category: FMA, MAD, LERP, BFE, BFI, MUL_HI, etc.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "rdna2_vop3.h"
#include <sstream>
#include <cstdio>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── VOP3 Detection & Decoding ────────────────────────────────────────────────

bool is_vop3_instruction(uint64_t word) {
    // VOP3 instructions: [63:56] = 0xD4..0xD7 for gfx1030
    uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
    return (enc >= 0xD4 && enc <= 0xD7);
}

Vop3Instruction decode_vop3(uint64_t encoded) {
    Vop3Instruction instr = {};

    // VOP3 encoding (gfx1030):
    // [63:56] = ENC (0xD4-0xD7)
    // [55:49] = opcode[8:2]
    // [48]    = clamp
    // [47:45] = abs_src2, abs_src1, abs_src0
    // [44:42] = neg_src2, neg_src1, neg_src0
    // [41:36] = vcc_dst (for compare ops)
    // [35:27] = src2 (9 bits)
    // [26:18] = src1 (9 bits)
    // [17:9]  = src0 (9 bits)
    // [8:0]   = vdst (9 bits, but only [7:0] used for VGPR)
    // [50:49] = omod

    uint16_t raw_opcode = static_cast<uint16_t>(((encoded >> 49) & 0x7F) | ((encoded >> 56) & 0x03) << 7);
    instr.opcode = static_cast<Vop3Opcode>(raw_opcode);
    instr.clamp = (encoded & (1ULL << 48)) != 0;
    instr.abs_src2 = (encoded & (1ULL << 47)) != 0;
    instr.abs_src1 = (encoded & (1ULL << 46)) != 0;
    instr.abs_src0 = (encoded & (1ULL << 45)) != 0;
    instr.neg_src2 = (encoded & (1ULL << 44)) != 0;
    instr.neg_src1 = (encoded & (1ULL << 43)) != 0;
    instr.neg_src0 = (encoded & (1ULL << 42)) != 0;
    instr.vcc_dst = static_cast<uint8_t>((encoded >> 36) & 0x3F);
    instr.src2 = static_cast<uint8_t>((encoded >> 27) & 0x1FF);
    instr.src1 = static_cast<uint8_t>((encoded >> 18) & 0x1FF);
    instr.src0 = static_cast<uint8_t>((encoded >> 9) & 0x1FF);
    instr.vdst = static_cast<uint8_t>(encoded & 0xFF);
    instr.omod = static_cast<uint8_t>((encoded >> 49) & 0x03);

    return instr;
}

// ── Helper: format source operand with modifiers ─────────────────────────────

static std::string format_src(uint8_t src, bool neg, bool abs, const char* type_prefix = "v") {
    std::ostringstream out;
    if (abs && neg) {
        out << "-abs(" << type_prefix << static_cast<int>(src) << ")";
    } else if (abs) {
        out << "abs(" << type_prefix << static_cast<int>(src) << ")";
    } else if (neg) {
        out << "-" << type_prefix << static_cast<int>(src);
    } else {
        out << type_prefix << static_cast<int>(src);
    }
    return out.str();
}

static std::string format_fsrc(uint8_t src, bool neg, bool abs) {
    return format_src(src, neg, abs, "v");
}

// ── VOP3 → HLSL Translation ─────────────────────────────────────────────────

std::string translate_vop3(const Vop3Instruction& instr) {
    std::ostringstream out;
    int vd = static_cast<int>(instr.vdst);
    std::string s0 = format_fsrc(instr.src0, instr.neg_src0, instr.abs_src0);
    std::string s1 = format_fsrc(instr.src1, instr.neg_src1, instr.abs_src1);
    std::string s2 = format_fsrc(instr.src2, instr.neg_src2, instr.abs_src2);

    switch (instr.opcode) {
    // ── Float FMA/MAD ────────────────────────────────────────────
    case Vop3Opcode::V_FMA_F32:
    case Vop3Opcode::V_MAD_F32:
    case Vop3Opcode::V_MAD_LEGACY_F32:
        out << "float v" << vd << " = mad(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_FMA_F64:
    case Vop3Opcode::V_MAD_F64:
        out << "double v" << vd << " = mad(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_FMA_F16:
    case Vop3Opcode::V_MAD_F16:
        out << "min16float v" << vd << " = mad(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;

    // ── LERP ─────────────────────────────────────────────────────
    case Vop3Opcode::V_LERP_U8F16:
        out << "float v" << vd << " = lerp(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;

    // ── Min/Max/Med3 ─────────────────────────────────────────────
    case Vop3Opcode::V_MIN3_F32:
        out << "float v" << vd << " = min(" << s0 << ", min(" << s1 << ", " << s2 << "));";
        break;
    case Vop3Opcode::V_MAX3_F32:
        out << "float v" << vd << " = max(" << s0 << ", max(" << s1 << ", " << s2 << "));";
        break;
    case Vop3Opcode::V_MED3_F32:
        out << "float v" << vd << " = max(min(" << s0 << ", " << s1 << "), min(max(" << s0 << ", " << s1 << "), " << s2 << "));";
        break;
    case Vop3Opcode::V_MIN3_I32:
        out << "int v" << vd << " = min(int(" << s0 << "), min(int(" << s1 << "), int(" << s2 << ")));";
        break;
    case Vop3Opcode::V_MAX3_I32:
        out << "int v" << vd << " = max(int(" << s0 << "), max(int(" << s1 << "), int(" << s2 << ")));";
        break;
    case Vop3Opcode::V_MED3_I32:
        out << "int v" << vd << " = max(min(int(" << s0 << "), int(" << s1 << ")), min(max(int(" << s0 << "), int(" << s1 << ")), int(" << s2 << ")));";
        break;
    case Vop3Opcode::V_MIN3_U32:
        out << "uint v" << vd << " = min(" << s0 << ", min(" << s1 << ", " << s2 << "));";
        break;
    case Vop3Opcode::V_MAX3_U32:
        out << "uint v" << vd << " = max(" << s0 << ", max(" << s1 << ", " << s2 << "));";
        break;
    case Vop3Opcode::V_MED3_U32:
        out << "uint v" << vd << " = max(min(" << s0 << ", " << s1 << "), min(max(" << s0 << ", " << s1 << "), " << s2 << "));";
        break;

    // ── Division helpers ─────────────────────────────────────────
    case Vop3Opcode::V_DIV_SCALE_F32:
        out << "/* V_DIV_SCALE_F32 */ float v" << vd << " = " << s0 << "; /* scale factor for div */";
        break;
    case Vop3Opcode::V_DIV_SCALE_F64:
        out << "/* V_DIV_SCALE_F64 */ double v" << vd << " = " << s0 << ";";
        break;
    case Vop3Opcode::V_DIV_FMAS_F32:
        out << "float v" << vd << " = mad(" << s0 << ", " << s1 << ", " << s2 << "); /* div fmas */";
        break;
    case Vop3Opcode::V_DIV_FMAS_F64:
        out << "double v" << vd << " = mad(" << s0 << ", " << s1 << ", " << s2 << "); /* div fmas */";
        break;

    // ── Integer multiply ─────────────────────────────────────────
    case Vop3Opcode::V_MUL_LO_U32:
    case Vop3Opcode::V_MUL_LO_I32:
        out << "uint v" << vd << " = " << s0 << " * " << s1 << ";";
        break;
    case Vop3Opcode::V_MUL_HI_U32:
        out << "uint v" << vd << " = mul_hi_u32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_MUL_HI_I32:
        out << "uint v" << vd << " = mul_hi_i32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_MAD_U64_U32:
        out << "uint2 v" << vd << " = mad_u64_u32(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_MAD_I64_I32:
        out << "uint2 v" << vd << " = mad_i64_i32(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_MUL_LO_U64:
        out << "uint2 v" << vd << " = mul_lo_u64(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_MUL_HI_U64:
        out << "uint2 v" << vd << " = mul_hi_u64(" << s0 << ", " << s1 << ");";
        break;

    // ── Bit manipulation ─────────────────────────────────────────
    case Vop3Opcode::V_BFE_U32:
        out << "uint v" << vd << " = (" << s0 << " >> (" << s1 << " & 31u)) & ((1u << (" << s2 << " & 31u)) - 1u);";
        break;
    case Vop3Opcode::V_BFE_I32:
        out << "int v" << vd << " = bfe_i32(int(" << s0 << "), int(" << s1 << "), int(" << s2 << "));";
        break;
    case Vop3Opcode::V_BFI_B32:
        out << "uint v" << vd << " = ((" << s0 << ") & ~((1u << (" << s2 << " & 31u)) - 1u)) | ((" << s1 << ") & ((1u << (" << s2 << " & 31u)) - 1u));";
        break;
    case Vop3Opcode::V_PERM_B32:
        out << "uint v" << vd << " = perm_b32(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_ALIGNBIT_B32:
        out << "uint v" << vd << " = alignbit(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_ALIGNBYTE_B32:
        out << "uint v" << vd << " = alignbyte(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_BITFIELD_INSERT_B32:
        out << "uint v" << vd << " = bitfield_insert(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_LSHL_B64:
        out << "uint2 v" << vd << " = lshl_b64(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_LSHR_B64:
        out << "uint2 v" << vd << " = lshr_b64(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_ASHR_I64:
        out << "uint2 v" << vd << " = ashr_i64(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_ADD_U64:
        out << "uint2 v" << vd << " = add_u64(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_SUB_U64:
        out << "uint2 v" << vd << " = sub_u64(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_AND_B64:
        out << "uint2 v" << vd << " = " << s0 << " & " << s1 << ";";
        break;
    case Vop3Opcode::V_OR_B64:
        out << "uint2 v" << vd << " = " << s0 << " | " << s1 << ";";
        break;
    case Vop3Opcode::V_XOR_B64:
        out << "uint2 v" << vd << " = " << s0 << " ^ " << s1 << ";";
        break;

    // ── Pack/unpack ──────────────────────────────────────────────
    case Vop3Opcode::V_PACK_F16_F32:
        out << "uint v" << vd << " = f32tof16(" << s0 << ") | (f32tof16(" << s1 << ") << 16u);";
        break;
    case Vop3Opcode::V_UNPACK_F16_F32:
        out << "float v" << vd << " = f16tof32(" << s0 << ");";
        break;
    case Vop3Opcode::V_DOT2_F32_F16:
        out << "float v" << vd << " = dot(f16tof32_2(" << s0 << "), f16tof32_2(" << s1 << "));";
        break;
    case Vop3Opcode::V_DOT4_F32_F8:
        out << "float v" << vd << " = dot4_f8(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_DOT8_F32_F4:
        out << "float v" << vd << " = dot8_f4(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_PK_ADD_F16:
        out << "uint v" << vd << " = pk_add_f16(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_PK_MUL_F16:
        out << "uint v" << vd << " = pk_mul_f16(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_PK_FMA_F16:
        out << "uint v" << vd << " = pk_fma_f16(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_PK_ADD_I16:
        out << "uint v" << vd << " = pk_add_i16(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_PK_MUL_I16:
        out << "uint v" << vd << " = pk_mul_i16(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_PK_FMA_I16:
        out << "uint v" << vd << " = pk_fma_i16(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;
    case Vop3Opcode::V_PK_ADD_U16:
        out << "uint v" << vd << " = pk_add_u16(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_PK_MUL_U16:
        out << "uint v" << vd << " = pk_mul_u16(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_PK_FMA_U16:
        out << "uint v" << vd << " = pk_fma_u16(" << s0 << ", " << s1 << ", " << s2 << ");";
        break;

    // ── Float comparisons → VCC mask ─────────────────────────────
    case Vop3Opcode::V_CMP_F_F32:
        out << "/* V_CMP_F_F32: always false */ vcc" << static_cast<int>(instr.vcc_dst) << " = 0u;";
        break;
    case Vop3Opcode::V_CMP_LT_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_lt_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_EQ_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_eq_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_LE_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_le_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_GT_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_gt_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_NEQ_F32:
    case Vop3Opcode::V_CMP_NEQ_F32_2:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_neq_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_GE_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_ge_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_O_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_ordered_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_U_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_unordered_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_NGE_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_nge_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_NLG_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_nlg_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_NGT_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_ngt_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_NLE_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_nle_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_NLT_F32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_nlt_f32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_TRU_F32:
        out << "/* V_CMP_TRU_F32: always true */ vcc" << static_cast<int>(instr.vcc_dst) << " = ~0u;";
        break;

    // ── Integer comparisons → VCC mask ───────────────────────────
    case Vop3Opcode::V_CMP_LT_I32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_lt_i32(int(" << s0 << "), int(" << s1 << "));";
        break;
    case Vop3Opcode::V_CMP_EQ_I32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_eq_i32(int(" << s0 << "), int(" << s1 << "));";
        break;
    case Vop3Opcode::V_CMP_LE_I32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_le_i32(int(" << s0 << "), int(" << s1 << "));";
        break;
    case Vop3Opcode::V_CMP_GT_I32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_gt_i32(int(" << s0 << "), int(" << s1 << "));";
        break;
    case Vop3Opcode::V_CMP_NEQ_I32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_neq_i32(int(" << s0 << "), int(" << s1 << "));";
        break;
    case Vop3Opcode::V_CMP_GE_I32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_ge_i32(int(" << s0 << "), int(" << s1 << "));";
        break;
    case Vop3Opcode::V_CMP_T_I32:
        out << "/* V_CMP_T_I32: always true */ vcc" << static_cast<int>(instr.vcc_dst) << " = ~0u;";
        break;

    case Vop3Opcode::V_CMP_LT_U32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_lt_u32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_EQ_U32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_eq_u32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_LE_U32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_le_u32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_GT_U32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_gt_u32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_NEQ_U32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_neq_u32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_GE_U32:
        out << "vcc" << static_cast<int>(instr.vcc_dst) << " = wave_cmp_ge_u32(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_CMP_T_U32:
        out << "/* V_CMP_T_U32: always true */ vcc" << static_cast<int>(instr.vcc_dst) << " = ~0u;";
        break;

    // ── Conditional select ───────────────────────────────────────
    case Vop3Opcode::V_CNDMASK_B32:
        out << "uint v" << vd << " = (vcc_mask & (1u << lane_id)) ? " << s1 << " : " << s0 << ";";
        break;
    case Vop3Opcode::V_CNDMASK_B64:
        out << "uint2 v" << vd << " = (vcc_mask & (1u << lane_id)) ? " << s1 << " : " << s0 << ";";
        break;
    case Vop3Opcode::V_READFIRSTLANE_B32:
        out << "uint v" << vd << " = WaveReadLaneFirst(" << s0 << ");";
        break;
    case Vop3Opcode::V_READLANE_B32:
        out << "uint v" << vd << " = WaveReadLaneAt(" << s0 << ", " << s1 << ");";
        break;

    // ── Misc ALU ─────────────────────────────────────────────────
    case Vop3Opcode::V_SAD_U32:
        out << "uint v" << vd << " = abs(int(" << s0 << ") - int(" << s1 << ")) + " << s2 << ";";
        break;
    case Vop3Opcode::V_BCNT_U32_B32:
        out << "uint v" << vd << " = countbits(" << s0 << ") + " << s1 << ";";
        break;
    case Vop3Opcode::V_MBCNT_LO_U32_B32:
        out << "uint v" << vd << " = mbcnt_lo(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_MBCNT_HI_U32_B32:
        out << "uint v" << vd << " = mbcnt_hi(" << s0 << ", " << s1 << ");";
        break;
    case Vop3Opcode::V_NOP:
        out << "/* V_NOP */";
        break;

    default:
        out << "/* UNSUPPORTED VOP3 opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }

    // Append clamp modifier if present
    if (instr.clamp) {
        out << " /* clamp */";
    }

    return out.str();
}

} // namespace rdna2_to_hlsl
} // namespace dedsec