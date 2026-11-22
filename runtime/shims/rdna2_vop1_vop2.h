// DedSec Fork — RDNA2 VOP1 + VOP2 Translator Header
// Translates 81 VOP1 (1-operand ALU) and 47 VOP2 (2-operand ALU) instruction forms to HLSL SM 6.6.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>

namespace dedsec {
namespace rdna2_to_hlsl {

// VOP1 opcodes (gfx1030, 81 forms)
enum class Vop1Opcode : uint8_t {
    V_MOV_B32          = 0x01,
    V_READFIRSTLANE_B32= 0x02,
    V_CVT_I32_F64      = 0x03,
    V_CVT_F64_I32      = 0x04,
    V_CVT_F32_I32      = 0x05,
    V_CVT_F32_U32      = 0x06,
    V_CVT_U32_F32      = 0x07,
    V_CVT_I32_F32      = 0x08,
    V_MOV_FED_B32      = 0x09,
    V_RCP_F32          = 0x0A,
    V_RSQ_F32          = 0x0B,
    V_SQRT_F32         = 0x0C,
    V_LOG_F32          = 0x0D,
    V_EXP_F32          = 0x0E,
    V_FREXP_MANT_F32   = 0x0F,
    V_FREXP_EXP_I32_F32= 0x10,
    V_FLOOR_F32        = 0x11,
    V_CEIL_F32         = 0x12,
    V_TRUNC_F32        = 0x13,
    V_RNDNE_F32        = 0x14,
    V_FRACT_F32        = 0x15,
    V_SIN_F32          = 0x17,
    V_COS_F32          = 0x18,
    V_NOT_B32          = 0x20,
    V_BFREV_B32        = 0x21,
    V_FFBL_B32         = 0x22,
    V_FFBB_B32         = 0x23,
    V_MOVRELS_B32      = 0x24,
    V_MOVRELD_B32      = 0x25,
    V_MOVRELSD_B32     = 0x26,
    V_CVT_F16_F32      = 0x30,
    V_CVT_F32_F16      = 0x31,
    V_CVT_RPI_I32_F32  = 0x32,
    V_CVT_FLR_I32_F32  = 0x33,
    V_CVT_OFF_F32_I4   = 0x34,
    V_CVT_F32_F64      = 0x35,
    V_CVT_F64_F32      = 0x36,
    V_CVT_F32_UBYTE0   = 0x37,
    V_CVT_F32_UBYTE1   = 0x38,
    V_CVT_F32_UBYTE2   = 0x39,
    V_CVT_F32_UBYTE3   = 0x3A,
    V_CVT_U32_F64      = 0x3B,
    V_CVT_F64_U32      = 0x3C,
    V_FRACT_F64        = 0x3D,
    V_FREXP_MANT_F64   = 0x3E,
    V_FREXP_EXP_I32_F64= 0x3F,
    V_FLOOR_F64        = 0x40,
    V_CEIL_F64         = 0x41,
    V_TRUNC_F64        = 0x42,
    V_RNDNE_F64        = 0x43,
    V_RCP_F64          = 0x44,
    V_RSQ_F64          = 0x45,
    V_SQRT_F64         = 0x46,
    V_LOG_F64          = 0x47,
    V_EXP_F64          = 0x48,
    V_LDEXP_F32        = 0x49,
    V_LDEXP_F64        = 0x4A,
    V_CVT_PKACC_U8_F32 = 0x4B,
    V_CVT_PKACC_I8_F32 = 0x4C,
    V_CVT_PKACC_U16_F32= 0x4D,
    V_CVT_PKACC_I16_F32= 0x4E,
    V_NOP              = 0x4F,
    V_ABS_I32          = 0x50,
    V_NEG_I32          = 0x51,
    V_BCNT_U32_B32     = 0x52,
    V_MBCNT_LO_U32_B32 = 0x53,
    V_MBCNT_HI_U32_B32 = 0x54,
    V_SAD_U32          = 0x55,
    V_READLANE_B32     = 0x56,
    V_WRITELANE_B32    = 0x57,
    V_SWAP_B32         = 0x58,
    V_MAX_F16          = 0x60,
    V_MIN_F16          = 0x61,
    V_MUL_F16          = 0x62,
    V_ADD_F16          = 0x63,
    V_SUB_F16          = 0x64,
    V_CVT_F32_F16_2    = 0x65,
    V_CVT_F16_F32_2    = 0x66,
    V_PACK_F16_F32_2   = 0x67,
};

// VOP2 opcodes (gfx1030, 47 forms)
enum class Vop2Opcode : uint8_t {
    V_ADD_F32          = 0x00,
    V_SUB_F32          = 0x01,
    V_MUL_F32          = 0x02,
    V_MIN_F32          = 0x03,
    V_MAX_F32          = 0x04,
    V_MAC_F32          = 0x05,
    V_MUL_LEGACY_F32   = 0x06,
    V_MAC_LEGACY_F32   = 0x07,
    V_ADD_I32          = 0x08,
    V_SUB_I32          = 0x09,
    V_SUBREV_I32       = 0x0A,
    V_MUL_I32_I24      = 0x0B,
    V_MUL_HI_I32_I24   = 0x0C,
    V_MUL_U32_U24      = 0x0D,
    V_MUL_HI_U32_U24   = 0x0E,
    V_AND_B32          = 0x10,
    V_OR_B32           = 0x11,
    V_XOR_B32          = 0x12,
    V_LSHL_B32         = 0x13,
    V_LSHR_B32         = 0x14,
    V_ASHR_I32         = 0x15,
    V_LSHLREV_B32      = 0x16,
    V_LSHRREV_B32      = 0x17,
    V_ASHRREV_I32      = 0x18,
    V_MIN_I32          = 0x20,
    V_MAX_I32          = 0x21,
    V_MIN_U32          = 0x22,
    V_MAX_U32          = 0x23,
    V_ADD_NC_U32       = 0x24,
    V_SUB_NC_U32       = 0x25,
    V_SUBREV_NC_U32    = 0x26,
    V_DOT2_F32_F16     = 0x30,
    V_DOT4_F32_F8      = 0x31,
    V_DOT8_F32_F4      = 0x32,
    V_PK_ADD_F16       = 0x33,
    V_PK_MUL_F16       = 0x34,
    V_PK_FMA_F16       = 0x35,
    V_PK_ADD_I16       = 0x36,
    V_PK_MUL_I16       = 0x37,
    V_PK_FMA_I16       = 0x38,
    V_PK_ADD_U16       = 0x39,
    V_PK_MUL_U16       = 0x3A,
    V_PK_FMA_U16       = 0x3B,
    V_ADD_F64          = 0x40,
    V_MUL_F64          = 0x41,
    V_MIN_F64          = 0x42,
    V_MAX_F64          = 0x43,
    V_LDEXP_F64        = 0x44,
};

struct Vop1Instruction {
    Vop1Opcode opcode;
    uint8_t vdst;
    uint8_t src0;
};

struct Vop2Instruction {
    Vop2Opcode opcode;
    uint8_t vdst;
    uint8_t src0;
    uint8_t src1;
};

Vop1Instruction decode_vop1(uint32_t encoded);
Vop2Instruction decode_vop2(uint32_t encoded);

std::string translate_vop1(const Vop1Instruction& instr);
std::string translate_vop2(const Vop2Instruction& instr);

bool is_vop1_instruction(uint32_t word);
bool is_vop2_instruction(uint32_t word);

} // namespace rdna2_to_hlsl
} // namespace dedsec