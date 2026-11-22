// DedSec Fork — RDNA2 VOP1 + VOP2 Translator Implementation
// Translates 81 VOP1 (1-operand ALU) and 47 VOP2 (2-operand ALU) instruction forms to HLSL SM 6.6.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "rdna2_vop1_vop2.h"
#include <sstream>
#include <cstdio>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── VOP1 Detection & Decoding ────────────────────────────────────────────────

bool is_vop1_instruction(uint32_t word) {
    // VOP1 instructions: [31:25] = 0x7E for gfx1030
    uint8_t enc = static_cast<uint8_t>((word >> 25) & 0x7F);
    return (enc == 0x7E);
}

Vop1Instruction decode_vop1(uint32_t encoded) {
    Vop1Instruction instr = {};
    // VOP1 encoding: [31:25]=0x7E, [24:17]=opcode(8 bits), [16:9]=vdst(8 bits), [8:0]=src0(8 bits)
    instr.opcode = static_cast<Vop1Opcode>((encoded >> 17) & 0xFF);
    instr.vdst   = static_cast<uint8_t>((encoded >> 9) & 0xFF);
    instr.src0   = static_cast<uint8_t>(encoded & 0xFF);
    return instr;
}

// ── VOP2 Detection & Decoding ────────────────────────────────────────────────

bool is_vop2_instruction(uint32_t word) {
    // VOP2 instructions: bit31=0, bit30=1 → [31:30] = 0b01
    return ((word >> 30) == 1u);
}

Vop2Instruction decode_vop2(uint32_t encoded) {
    Vop2Instruction instr = {};
    // VOP2 encoding: [31]=0, [30]=1, [29:23]=opcode(7 bits), [22:16]=vdst, [15:8]=src0, [7:0]=src1
    instr.opcode = static_cast<Vop2Opcode>((encoded >> 23) & 0x7F);
    instr.vdst   = static_cast<uint8_t>((encoded >> 16) & 0x7F);
    instr.src0   = static_cast<uint8_t>((encoded >> 8) & 0xFF);
    instr.src1   = static_cast<uint8_t>(encoded & 0xFF);
    return instr;
}

// ── VOP1 → HLSL Translation ─────────────────────────────────────────────────

std::string translate_vop1(const Vop1Instruction& instr) {
    std::ostringstream out;
    int vd = static_cast<int>(instr.vdst);
    int s0 = static_cast<int>(instr.src0);

    switch (instr.opcode) {
    // ── Move / lane read ──────────────────────────────────────
    case Vop1Opcode::V_MOV_B32:
        out << "uint v" << vd << " = v" << s0 << ";";
        break;
    case Vop1Opcode::V_READFIRSTLANE_B32:
        out << "uint v" << vd << " = WaveReadLaneFirst(v" << s0 << ");";
        break;
    case Vop1Opcode::V_READLANE_B32:
        out << "uint v" << vd << " = WaveReadLaneAt(v" << s0 << ", lane_id);";
        break;
    case Vop1Opcode::V_WRITELANE_B32:
        out << "/* V_WRITELANE_B32 */ v" << vd << "[lane_id] = v" << s0 << ";";
        break;
    case Vop1Opcode::V_SWAP_B32:
        out << "uint tmp_swap = v" << vd << "; v" << vd << " = v" << s0 << "; v" << s0 << " = tmp_swap;";
        break;

    // ── Float conversions ─────────────────────────────────────
    case Vop1Opcode::V_CVT_F32_I32:
        out << "float v" << vd << " = float(int(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_F32_U32:
        out << "float v" << vd << " = float(v" << s0 << ");";
        break;
    case Vop1Opcode::V_CVT_U32_F32:
        out << "uint v" << vd << " = uint(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_I32_F32:
        out << "uint v" << vd << " = uint(int(asfloat(v" << s0 << ")));";
        break;
    case Vop1Opcode::V_CVT_I32_F64:
        out << "uint v" << vd << " = uint(int(asdouble(v" << s0 << "_lo, v" << s0 << "_hi)));";
        break;
    case Vop1Opcode::V_CVT_F64_I32:
        out << "/* V_CVT_F64_I32 */ double d" << vd << " = double(int(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_U32_F64:
        out << "uint v" << vd << " = uint(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_CVT_F64_U32:
        out << "/* V_CVT_F64_U32 */ double d" << vd << " = double(v" << s0 << ");";
        break;
    case Vop1Opcode::V_CVT_F32_F64:
        out << "float v" << vd << " = float(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_CVT_F64_F32:
        out << "/* V_CVT_F64_F32 */ double d" << vd << " = double(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_F16_F32:
        out << "uint v" << vd << " = f32tof16(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_F32_F16:
        out << "float v" << vd << " = f16tof32(v" << s0 << ");";
        break;
    case Vop1Opcode::V_CVT_F32_F16_2:
        out << "float v" << vd << " = f16tof32(v" << s0 << ");";
        break;
    case Vop1Opcode::V_CVT_F16_F32_2:
        out << "uint v" << vd << " = f32tof16(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_PACK_F16_F32_2:
        out << "uint v" << vd << " = f32tof16(asfloat(v" << s0 << ")) | (f32tof16(asfloat(v" << s0 << ")) << 16u);";
        break;

    // ── Integer conversions ───────────────────────────────────
    case Vop1Opcode::V_CVT_RPI_I32_F32:
        out << "uint v" << vd << " = uint(int(asfloat(v" << s0 << ") + 0.5f));";
        break;
    case Vop1Opcode::V_CVT_FLR_I32_F32:
        out << "uint v" << vd << " = uint(int(floor(asfloat(v" << s0 << "))));";
        break;
    case Vop1Opcode::V_CVT_OFF_F32_I4:
        out << "float v" << vd << " = float(int(v" << s0 << " & 0xFu)) - 8.0f;";
        break;
    case Vop1Opcode::V_CVT_F32_UBYTE0:
        out << "float v" << vd << " = float(v" << s0 << " & 0xFFu);";
        break;
    case Vop1Opcode::V_CVT_F32_UBYTE1:
        out << "float v" << vd << " = float((v" << s0 << " >> 8u) & 0xFFu);";
        break;
    case Vop1Opcode::V_CVT_F32_UBYTE2:
        out << "float v" << vd << " = float((v" << s0 << " >> 16u) & 0xFFu);";
        break;
    case Vop1Opcode::V_CVT_F32_UBYTE3:
        out << "float v" << vd << " = float((v" << s0 << " >> 24u) & 0xFFu);";
        break;
    case Vop1Opcode::V_CVT_PKACC_U8_F32:
        out << "uint v" << vd << " = pack_u8_f32(v" << vd << ", asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_PKACC_I8_F32:
        out << "uint v" << vd << " = pack_i8_f32(v" << vd << ", asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_PKACC_U16_F32:
        out << "uint v" << vd << " = pack_u16_f32(v" << vd << ", asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CVT_PKACC_I16_F32:
        out << "uint v" << vd << " = pack_i16_f32(v" << vd << ", asfloat(v" << s0 << "));";
        break;

    // ── Float math ────────────────────────────────────────────
    case Vop1Opcode::V_RCP_F32:
        out << "float v" << vd << " = rcp(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_RSQ_F32:
        out << "float v" << vd << " = rsqrt(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_SQRT_F32:
        out << "float v" << vd << " = sqrt(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_LOG_F32:
        out << "float v" << vd << " = log2(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_EXP_F32:
        out << "float v" << vd << " = exp2(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_FREXP_MANT_F32:
        out << "float v" << vd << " = frexp_mant(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_FREXP_EXP_I32_F32:
        out << "uint v" << vd << " = uint(frexp_exp(asfloat(v" << s0 << ")));";
        break;
    case Vop1Opcode::V_FLOOR_F32:
        out << "float v" << vd << " = floor(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_CEIL_F32:
        out << "float v" << vd << " = ceil(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_TRUNC_F32:
        out << "float v" << vd << " = trunc(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_RNDNE_F32:
        out << "float v" << vd << " = round(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_FRACT_F32:
        out << "float v" << vd << " = frac(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_SIN_F32:
        out << "float v" << vd << " = sin(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_COS_F32:
        out << "float v" << vd << " = cos(asfloat(v" << s0 << "));";
        break;
    case Vop1Opcode::V_LDEXP_F32:
        out << "float v" << vd << " = ldexp(asfloat(v" << s0 << "), exp_val);";
        break;

    // ── Double math ───────────────────────────────────────────
    case Vop1Opcode::V_FRACT_F64:
        out << "/* V_FRACT_F64 */ double d" << vd << " = frac(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_FREXP_MANT_F64:
        out << "/* V_FREXP_MANT_F64 */ double d" << vd << " = frexp_mant(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_FREXP_EXP_I32_F64:
        out << "uint v" << vd << " = uint(frexp_exp(asdouble(v" << s0 << "_lo, v" << s0 << "_hi)));";
        break;
    case Vop1Opcode::V_FLOOR_F64:
        out << "/* V_FLOOR_F64 */ double d" << vd << " = floor(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_CEIL_F64:
        out << "/* V_CEIL_F64 */ double d" << vd << " = ceil(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_TRUNC_F64:
        out << "/* V_TRUNC_F64 */ double d" << vd << " = trunc(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_RNDNE_F64:
        out << "/* V_RNDNE_F64 */ double d" << vd << " = round(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_RCP_F64:
        out << "/* V_RCP_F64 */ double d" << vd << " = rcp(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_RSQ_F64:
        out << "/* V_RSQ_F64 */ double d" << vd << " = rsqrt(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_SQRT_F64:
        out << "/* V_SQRT_F64 */ double d" << vd << " = sqrt(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_LOG_F64:
        out << "/* V_LOG_F64 */ double d" << vd << " = log2(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_EXP_F64:
        out << "/* V_EXP_F64 */ double d" << vd << " = exp2(asdouble(v" << s0 << "_lo, v" << s0 << "_hi));";
        break;
    case Vop1Opcode::V_LDEXP_F64:
        out << "/* V_LDEXP_F64 */ double d" << vd << " = ldexp(asdouble(v" << s0 << "_lo, v" << s0 << "_hi), exp_val);";
        break;

    // ── Bit manipulation ──────────────────────────────────────
    case Vop1Opcode::V_NOT_B32:
        out << "uint v" << vd << " = ~v" << s0 << ";";
        break;
    case Vop1Opcode::V_BFREV_B32:
        out << "uint v" << vd << " = reversebits(v" << s0 << ");";
        break;
    case Vop1Opcode::V_FFBL_B32:
        out << "uint v" << vd << " = firstbitlow(v" << s0 << ");";
        break;
    case Vop1Opcode::V_FFBB_B32:
        out << "uint v" << vd << " = firstbithigh(v" << s0 << ");";
        break;

    // ── Indirect moves ────────────────────────────────────────
    case Vop1Opcode::V_MOVRELS_B32:
        out << "uint v" << vd << " = vgpr_rel[v" << s0 << "]; /* MOVRELS */";
        break;
    case Vop1Opcode::V_MOVRELD_B32:
        out << "vgpr_rel[v" << vd << "] = v" << s0 << "; /* MOVRELD */";
        break;
    case Vop1Opcode::V_MOVRELSD_B32:
        out << "vgpr_rel[v" << vd << "] = v" << s0 << "; /* MOVRELSD */";
        break;
    case Vop1Opcode::V_MOV_FED_B32:
        out << "uint v" << vd << " = v" << s0 << "; /* FED */";
        break;

    // ── Integer ALU ───────────────────────────────────────────
    case Vop1Opcode::V_ABS_I32:
        out << "uint v" << vd << " = uint(abs(int(v" << s0 << ")));";
        break;
    case Vop1Opcode::V_NEG_I32:
        out << "uint v" << vd << " = uint(-int(v" << s0 << "));";
        break;
    case Vop1Opcode::V_BCNT_U32_B32:
        out << "uint v" << vd << " = countbits(v" << s0 << ");";
        break;
    case Vop1Opcode::V_MBCNT_LO_U32_B32:
        out << "uint v" << vd << " = mbcnt_lo(v" << s0 << ");";
        break;
    case Vop1Opcode::V_MBCNT_HI_U32_B32:
        out << "uint v" << vd << " = mbcnt_hi(v" << s0 << ");";
        break;
    case Vop1Opcode::V_SAD_U32:
        out << "uint v" << vd << " = abs(int(v" << s0 << ") - int(sad_src1)) + sad_accum;";
        break;

    // ── Half-float ALU ────────────────────────────────────────
    case Vop1Opcode::V_MAX_F16:
        out << "min16float v" << vd << " = max(f16tof32(v" << s0 << "), f16_max_ref);";
        break;
    case Vop1Opcode::V_MIN_F16:
        out << "min16float v" << vd << " = min(f16tof32(v" << s0 << "), f16_min_ref);";
        break;
    case Vop1Opcode::V_MUL_F16:
        out << "min16float v" << vd << " = f16tof32(v" << s0 << ") * f16_mul_ref;";
        break;
    case Vop1Opcode::V_ADD_F16:
        out << "min16float v" << vd << " = f16tof32(v" << s0 << ") + f16_add_ref;";
        break;
    case Vop1Opcode::V_SUB_F16:
        out << "min16float v" << vd << " = f16tof32(v" << s0 << ") - f16_sub_ref;";
        break;

    // ── NOP ───────────────────────────────────────────────────
    case Vop1Opcode::V_NOP:
        out << "/* V_NOP */";
        break;

    default:
        out << "/* UNSUPPORTED VOP1 opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }

    return out.str();
}

// ── VOP2 → HLSL Translation ─────────────────────────────────────────────────

std::string translate_vop2(const Vop2Instruction& instr) {
    std::ostringstream out;
    int vd = static_cast<int>(instr.vdst);
    int s0 = static_cast<int>(instr.src0);
    int s1 = static_cast<int>(instr.src1);

    switch (instr.opcode) {
    // ── Float ALU ─────────────────────────────────────────────
    case Vop2Opcode::V_ADD_F32:
        out << "float v" << vd << " = asfloat(v" << s0 << ") + asfloat(v" << s1 << ");";
        break;
    case Vop2Opcode::V_SUB_F32:
        out << "float v" << vd << " = asfloat(v" << s0 << ") - asfloat(v" << s1 << ");";
        break;
    case Vop2Opcode::V_MUL_F32:
        out << "float v" << vd << " = asfloat(v" << s0 << ") * asfloat(v" << s1 << ");";
        break;
    case Vop2Opcode::V_MIN_F32:
        out << "float v" << vd << " = min(asfloat(v" << s0 << "), asfloat(v" << s1 << "));";
        break;
    case Vop2Opcode::V_MAX_F32:
        out << "float v" << vd << " = max(asfloat(v" << s0 << "), asfloat(v" << s1 << "));";
        break;
    case Vop2Opcode::V_MAC_F32:
        out << "float v" << vd << " = mad(asfloat(v" << s0 << "), asfloat(v" << s1 << "), asfloat(v" << vd << "));";
        break;
    case Vop2Opcode::V_MUL_LEGACY_F32:
        out << "float v" << vd << " = asfloat(v" << s0 << ") * asfloat(v" << s1 << "); /* legacy */";
        break;
    case Vop2Opcode::V_MAC_LEGACY_F32:
        out << "float v" << vd << " = mad(asfloat(v" << s0 << "), asfloat(v" << s1 << "), asfloat(v" << vd << ")); /* legacy */";
        break;

    // ── Double ALU ────────────────────────────────────────────
    case Vop2Opcode::V_ADD_F64:
        out << "/* V_ADD_F64 */ double d" << vd << " = asdouble(v" << s0 << "_lo,v" << s0 << "_hi) + asdouble(v" << s1 << "_lo,v" << s1 << "_hi);";
        break;
    case Vop2Opcode::V_MUL_F64:
        out << "/* V_MUL_F64 */ double d" << vd << " = asdouble(v" << s0 << "_lo,v" << s0 << "_hi) * asdouble(v" << s1 << "_lo,v" << s1 << "_hi);";
        break;
    case Vop2Opcode::V_MIN_F64:
        out << "/* V_MIN_F64 */ double d" << vd << " = min(asdouble(v" << s0 << "_lo,v" << s0 << "_hi), asdouble(v" << s1 << "_lo,v" << s1 << "_hi));";
        break;
    case Vop2Opcode::V_MAX_F64:
        out << "/* V_MAX_F64 */ double d" << vd << " = max(asdouble(v" << s0 << "_lo,v" << s0 << "_hi), asdouble(v" << s1 << "_lo,v" << s1 << "_hi));";
        break;
    case Vop2Opcode::V_LDEXP_F64:
        out << "/* V_LDEXP_F64 */ double d" << vd << " = ldexp(asdouble(v" << s0 << "_lo,v" << s0 << "_hi), int(v" << s1 << "));";
        break;

    // ── Integer ALU ───────────────────────────────────────────
    case Vop2Opcode::V_ADD_I32:
        out << "uint v" << vd << " = uint(int(v" << s0 << ") + int(v" << s1 << "));";
        break;
    case Vop2Opcode::V_SUB_I32:
        out << "uint v" << vd << " = uint(int(v" << s0 << ") - int(v" << s1 << "));";
        break;
    case Vop2Opcode::V_SUBREV_I32:
        out << "uint v" << vd << " = uint(int(v" << s1 << ") - int(v" << s0 << "));";
        break;
    case Vop2Opcode::V_MUL_I32_I24:
        out << "uint v" << vd << " = uint(int(v" << s0 << ") * int(v" << s1 << ")); /* i24 */";
        break;
    case Vop2Opcode::V_MUL_HI_I32_I24:
        out << "uint v" << vd << " = mul_hi_i32(int(v" << s0 << "), int(v" << s1 << ")); /* i24 */";
        break;
    case Vop2Opcode::V_MUL_U32_U24:
        out << "uint v" << vd << " = v" << s0 << " * v" << s1 << "; /* u24 */";
        break;
    case Vop2Opcode::V_MUL_HI_U32_U24:
        out << "uint v" << vd << " = mul_hi_u32(v" << s0 << ", v" << s1 << "); /* u24 */";
        break;
    case Vop2Opcode::V_ADD_NC_U32:
        out << "uint v" << vd << " = v" << s0 << " + v" << s1 << "; /* no carry */";
        break;
    case Vop2Opcode::V_SUB_NC_U32:
        out << "uint v" << vd << " = v" << s0 << " - v" << s1 << "; /* no carry */";
        break;
    case Vop2Opcode::V_SUBREV_NC_U32:
        out << "uint v" << vd << " = v" << s1 << " - v" << s0 << "; /* no carry */";
        break;

    // ── Bitwise ALU ───────────────────────────────────────────
    case Vop2Opcode::V_AND_B32:
        out << "uint v" << vd << " = v" << s0 << " & v" << s1 << ";";
        break;
    case Vop2Opcode::V_OR_B32:
        out << "uint v" << vd << " = v" << s0 << " | v" << s1 << ";";
        break;
    case Vop2Opcode::V_XOR_B32:
        out << "uint v" << vd << " = v" << s0 << " ^ v" << s1 << ";";
        break;
    case Vop2Opcode::V_LSHL_B32:
        out << "uint v" << vd << " = v" << s0 << " << (v" << s1 << " & 31u);";
        break;
    case Vop2Opcode::V_LSHR_B32:
        out << "uint v" << vd << " = v" << s0 << " >> (v" << s1 << " & 31u);";
        break;
    case Vop2Opcode::V_ASHR_I32:
        out << "uint v" << vd << " = uint(int(v" << s0 << ") >> int(v" << s1 << " & 31u));";
        break;
    case Vop2Opcode::V_LSHLREV_B32:
        out << "uint v" << vd << " = v" << s0 << " << (v" << s1 << " & 31u); /* rev */";
        break;
    case Vop2Opcode::V_LSHRREV_B32:
        out << "uint v" << vd << " = v" << s0 << " >> (v" << s1 << " & 31u); /* rev */";
        break;
    case Vop2Opcode::V_ASHRREV_I32:
        out << "uint v" << vd << " = uint(int(v" << s0 << ") >> int(v" << s1 << " & 31u)); /* rev */";
        break;

    // ── Min/Max integer ───────────────────────────────────────
    case Vop2Opcode::V_MIN_I32:
        out << "uint v" << vd << " = uint(min(int(v" << s0 << "), int(v" << s1 << ")));";
        break;
    case Vop2Opcode::V_MAX_I32:
        out << "uint v" << vd << " = uint(max(int(v" << s0 << "), int(v" << s1 << ")));";
        break;
    case Vop2Opcode::V_MIN_U32:
        out << "uint v" << vd << " = min(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_MAX_U32:
        out << "uint v" << vd << " = max(v" << s0 << ", v" << s1 << ");";
        break;

    // ── Dot products ──────────────────────────────────────────
    case Vop2Opcode::V_DOT2_F32_F16:
        out << "float v" << vd << " = dot(f16tof32_2(v" << s0 << "), f16tof32_2(v" << s1 << "));";
        break;
    case Vop2Opcode::V_DOT4_F32_F8:
        out << "float v" << vd << " = dot4_f8(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_DOT8_F32_F4:
        out << "float v" << vd << " = dot8_f4(v" << s0 << ", v" << s1 << ");";
        break;

    // ── Packed half-float ALU ─────────────────────────────────
    case Vop2Opcode::V_PK_ADD_F16:
        out << "uint v" << vd << " = pk_add_f16(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_PK_MUL_F16:
        out << "uint v" << vd << " = pk_mul_f16(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_PK_FMA_F16:
        out << "uint v" << vd << " = pk_fma_f16(v" << s0 << ", v" << s1 << ", v" << vd << ");";
        break;
    case Vop2Opcode::V_PK_ADD_I16:
        out << "uint v" << vd << " = pk_add_i16(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_PK_MUL_I16:
        out << "uint v" << vd << " = pk_mul_i16(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_PK_FMA_I16:
        out << "uint v" << vd << " = pk_fma_i16(v" << s0 << ", v" << s1 << ", v" << vd << ");";
        break;
    case Vop2Opcode::V_PK_ADD_U16:
        out << "uint v" << vd << " = pk_add_u16(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_PK_MUL_U16:
        out << "uint v" << vd << " = pk_mul_u16(v" << s0 << ", v" << s1 << ");";
        break;
    case Vop2Opcode::V_PK_FMA_U16:
        out << "uint v" << vd << " = pk_fma_u16(v" << s0 << ", v" << s1 << ", v" << vd << ");";
        break;

    default:
        out << "/* UNSUPPORTED VOP2 opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }

    return out.str();
}

} // namespace rdna2_to_hlsl
} // namespace dedsec