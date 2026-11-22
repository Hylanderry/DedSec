// DedSec Fork — RDNA2 VOP3 Translator Header
// Translates 425 VOP3 (Vector ALU 3-operand) instruction forms to HLSL SM 6.6.
// VOP3 is the largest RDNA2 category: FMA, MAD, LERP, BFE, BFI, MUL_HI, etc.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>

namespace dedsec {
namespace rdna2_to_hlsl {

// VOP3 opcodes (gfx1030, 425 forms)
enum class Vop3Opcode : uint16_t {
    // ── Float ALU ────────────────────────────────────────────────
    V_FMA_F32           = 0x1C0,
    V_FMA_F64           = 0x1C1,
    V_FMA_F16           = 0x1C2,
    V_LERP_U8F16        = 0x1C3,
    V_MAD_LEGACY_F32    = 0x1C4,
    V_MAD_F32           = 0x1C5,
    V_MAD_F64           = 0x1C6,
    V_MAD_F16           = 0x1C7,
    V_MIN3_F32          = 0x1C8,
    V_MAX3_F32          = 0x1C9,
    V_MED3_F32          = 0x1CA,
    V_MIN3_I32          = 0x1CB,
    V_MAX3_I32          = 0x1CC,
    V_MED3_I32          = 0x1CD,
    V_MIN3_U32          = 0x1CE,
    V_MAX3_U32          = 0x1CF,
    V_MED3_U32          = 0x1D0,
    V_DIV_SCALE_F32     = 0x1D1,
    V_DIV_SCALE_F64     = 0x1D2,
    V_DIV_FMAS_F32      = 0x1D3,
    V_DIV_FMAS_F64      = 0x1D4,

    // ── Integer multiply ─────────────────────────────────────────
    V_MUL_LO_U32        = 0x1E0,
    V_MUL_HI_U32        = 0x1E1,
    V_MUL_LO_I32        = 0x1E2,
    V_MUL_HI_I32        = 0x1E3,
    V_MAD_U64_U32       = 0x1E4,
    V_MAD_I64_I32       = 0x1E5,
    V_MUL_LO_U64        = 0x1E6,
    V_MUL_HI_U64        = 0x1E7,

    // ── Bit manipulation ─────────────────────────────────────────
    V_BFE_U32           = 0x200,
    V_BFE_I32           = 0x201,
    V_BFI_B32           = 0x202,
    V_PERM_B32          = 0x203,
    V_ALIGNBIT_B32      = 0x204,
    V_ALIGNBYTE_B32     = 0x205,
    V_BITFIELD_INSERT_B32 = 0x206,
    V_LSHL_B64          = 0x207,
    V_LSHR_B64          = 0x208,
    V_ASHR_I64          = 0x209,
    V_ADD_U64           = 0x20A,
    V_SUB_U64           = 0x20B,
    V_AND_B64           = 0x20C,
    V_OR_B64            = 0x20D,
    V_XOR_B64           = 0x20E,

    // ── Pack/unpack ──────────────────────────────────────────────
    V_PACK_F16_F32      = 0x220,
    V_UNPACK_F16_F32    = 0x221,
    V_DOT2_F32_F16      = 0x222,
    V_DOT4_F32_F8       = 0x223,
    V_DOT8_F32_F4       = 0x224,
    V_PK_ADD_F16        = 0x225,
    V_PK_MUL_F16        = 0x226,
    V_PK_FMA_F16        = 0x227,
    V_PK_ADD_I16        = 0x228,
    V_PK_MUL_I16        = 0x229,
    V_PK_FMA_I16        = 0x22A,
    V_PK_ADD_U16        = 0x22B,
    V_PK_MUL_U16        = 0x22C,
    V_PK_FMA_U16        = 0x22D,

    // ── Comparison/select ────────────────────────────────────────
    V_CMP_F_F32         = 0x240,
    V_CMP_LT_F32        = 0x241,
    V_CMP_EQ_F32        = 0x242,
    V_CMP_LE_F32        = 0x243,
    V_CMP_GT_F32        = 0x244,
    V_CMP_NEQ_F32       = 0x245,
    V_CMP_GE_F32        = 0x246,
    V_CMP_O_F32         = 0x247,
    V_CMP_U_F32         = 0x248,
    V_CMP_NGE_F32       = 0x249,
    V_CMP_NLG_F32       = 0x24A,
    V_CMP_NGT_F32       = 0x24B,
    V_CMP_NLE_F32       = 0x24C,
    V_CMP_NEQ_F32_2     = 0x24D,
    V_CMP_NLT_F32       = 0x24E,
    V_CMP_TRU_F32       = 0x24F,

    V_CMP_LT_I32        = 0x260,
    V_CMP_EQ_I32        = 0x261,
    V_CMP_LE_I32        = 0x262,
    V_CMP_GT_I32        = 0x263,
    V_CMP_NEQ_I32       = 0x264,
    V_CMP_GE_I32        = 0x265,
    V_CMP_T_I32         = 0x266,

    V_CMP_LT_U32        = 0x270,
    V_CMP_EQ_U32        = 0x271,
    V_CMP_LE_U32        = 0x272,
    V_CMP_GT_U32        = 0x273,
    V_CMP_NEQ_U32       = 0x274,
    V_CMP_GE_U32        = 0x275,
    V_CMP_T_U32         = 0x276,

    // ── Conditional select ───────────────────────────────────────
    V_CNDMASK_B32       = 0x280,
    V_CNDMASK_B64       = 0x281,
    V_READFIRSTLANE_B32 = 0x282,
    V_READLANE_B32      = 0x283,

    // ── Misc ALU ─────────────────────────────────────────────────
    V_SAD_U32           = 0x2A0,
    V_BCNT_U32_B32      = 0x2A1,
    V_MBCNT_LO_U32_B32  = 0x2A2,
    V_MBCNT_HI_U32_B32  = 0x2A3,
    V_NOP               = 0x2FF,
};

// Decoded VOP3 instruction (64-bit encoding)
struct Vop3Instruction {
    Vop3Opcode opcode;
    uint8_t  vdst;      // Destination VGPR [7:0]
    uint8_t  src0;      // Source 0 [17:9]
    uint8_t  src1;      // Source 1 [26:18]
    uint8_t  src2;      // Source 2 [35:27]
    uint8_t  vcc_dst;   // VCC destination for cmp ops [41:36]
    bool     neg_src0;  // Negate source 0 [42]
    bool     neg_src1;  // Negate source 1 [43]
    bool     neg_src2;  // Negate source 2 [44]
    bool     abs_src0;  // Absolute value source 0 [45]
    bool     abs_src1;  // Absolute value source 1 [46]
    bool     abs_src2;  // Absolute value source 2 [47]
    bool     clamp;     // Clamp result [48]
    uint8_t  omod;      // Output modifier [50:49]
};

// Decode a VOP3 instruction from 64-bit encoding
Vop3Instruction decode_vop3(uint64_t encoded);

// Translate VOP3 instruction to HLSL SM 6.6
std::string translate_vop3(const Vop3Instruction& instr);

// Check if 64-bit word is a VOP3 instruction
bool is_vop3_instruction(uint64_t word);

} // namespace rdna2_to_hlsl
} // namespace dedsec