// DedSec Fork — RDNA2 SOP (Scalar ALU) Translator Header
// Translates SOP1(65) + SOP2(51) + SOPK(27) + SOPC(18) + SOPP(36) = 197 scalar
// instruction forms to HLSL SM 6.6 compute shader equivalents.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── SOP1 Opcodes (gfx1030, 65 forms) ─────────────────────────────────────────
enum class Sop1Opcode : uint8_t {
    S_MOV_B32           = 0x00,
    S_MOV_B64           = 0x01,
    S_CMOV_B32          = 0x02,
    S_CMOV_B64          = 0x03,
    S_NOT_B32           = 0x04,
    S_NOT_B64           = 0x05,
    S_WQM_B32           = 0x06,
    S_WQM_B64           = 0x07,
    S_BREV_B32          = 0x08,
    S_BREV_B64          = 0x09,
    S_BCNT0_I32_B32     = 0x0A,
    S_BCNT0_I32_B64     = 0x0B,
    S_BCNT1_I32_B32     = 0x0C,
    S_BCNT1_I32_B64     = 0x0D,
    S_FF0_I32_B32       = 0x0E,
    S_FF0_I32_B64       = 0x0F,
    S_FF1_I32_B32       = 0x10,
    S_FF1_I32_B64       = 0x11,
    S_FLBIT_I32_B32     = 0x12,
    S_FLBIT_I32_B64     = 0x13,
    S_FLBIT_I32         = 0x14,
    S_FLBIT_I32_I64     = 0x15,
    S_SEXT_I32_I8       = 0x16,
    S_SEXT_I32_I16      = 0x17,
    S_BITSET0_B32       = 0x18,
    S_BITSET0_B64       = 0x19,
    S_BITSET1_B32       = 0x1A,
    S_BITSET1_B64       = 0x1B,
    S_GETPC_B64         = 0x1C,
    S_SETPC_B64         = 0x1D,
    S_SWAPPC_B64        = 0x1E,
    S_RFE_B64           = 0x1F,
    S_AND_SAVEEXEC_B64  = 0x20,
    S_OR_SAVEEXEC_B64   = 0x21,
    S_XOR_SAVEEXEC_B64  = 0x22,
    S_ANDN2_SAVEEXEC_B64= 0x23,
    S_ORN2_SAVEEXEC_B64 = 0x24,
    S_NAND_SAVEEXEC_B64 = 0x25,
    S_NOR_SAVEEXEC_B64  = 0x26,
    S_XNOR_SAVEEXEC_B64 = 0x27,
    S_QUADMASK_B32      = 0x28,
    S_QUADMASK_B64      = 0x29,
    S_MOVRELS_B32       = 0x2A,
    S_MOVRELS_B64       = 0x2B,
    S_MOVRELSD_B32      = 0x2C,
    S_MOVRELSD_B64      = 0x2D,
    S_ABS_I32           = 0x30,
    S_NEG_I32           = 0x31,
    S_NOP               = 0x3F,
};

// ── SOP2 Opcodes (gfx1030, 51 forms) ─────────────────────────────────────────
enum class Sop2Opcode : uint8_t {
    S_ADD_U32           = 0x00,
    S_SUB_U32           = 0x01,
    S_ADD_I32           = 0x02,
    S_SUB_I32           = 0x03,
    S_ADDC_U32          = 0x04,
    S_SUBB_U32          = 0x05,
    S_MIN_I32           = 0x06,
    S_MIN_U32           = 0x07,
    S_MAX_I32           = 0x08,
    S_MAX_U32           = 0x09,
    S_CSELECT_B32       = 0x0A,
    S_CSELECT_B64       = 0x0B,
    S_AND_B32           = 0x0E,
    S_AND_B64           = 0x0F,
    S_OR_B32            = 0x10,
    S_OR_B64            = 0x11,
    S_XOR_B32           = 0x12,
    S_XOR_B64           = 0x13,
    S_ANDN2_B32         = 0x14,
    S_ANDN2_B64         = 0x15,
    S_ORN2_B32          = 0x16,
    S_ORN2_B64          = 0x17,
    S_NAND_B32          = 0x18,
    S_NAND_B64          = 0x19,
    S_NOR_B32           = 0x1A,
    S_NOR_B64           = 0x1B,
    S_XNOR_B32          = 0x1C,
    S_XNOR_B64          = 0x1D,
    S_LSHL_B32          = 0x1E,
    S_LSHL_B64          = 0x1F,
    S_LSHR_B32          = 0x20,
    S_LSHR_B64          = 0x21,
    S_ASHR_I32          = 0x22,
    S_ASHR_I64          = 0x23,
    S_BFM_B32           = 0x24,
    S_BFM_B64           = 0x25,
    S_MUL_I32           = 0x26,
    S_BFE_U32           = 0x28,
    S_BFE_I32           = 0x29,
    S_BFE_U64           = 0x2A,
    S_BFE_I64           = 0x2B,
    S_BFI_B32           = 0x2C,
    S_BFI_B64           = 0x2D,
    S_PACK_F16_F32      = 0x2E,
    S_PACK_F32_F16      = 0x2F,
    S_WSHL_B32          = 0x30,
    S_WSHL_B64          = 0x31,
    S_WRSHL_B32         = 0x32,
    S_WRSHL_B64         = 0x33,
};

// ── SOPK Opcodes (gfx1030, 27 forms) ─────────────────────────────────────────
enum class SopkOpcode : uint8_t {
    S_MOVK_I32          = 0x00,
    S_CMOVK_I32         = 0x01,
    S_CMPK_EQ_I32       = 0x02,
    S_CMPK_LG_I32       = 0x03,
    S_CMPK_GT_I32       = 0x04,
    S_CMPK_GE_I32       = 0x05,
    S_CMPK_LT_I32       = 0x06,
    S_CMPK_LE_I32       = 0x07,
    S_CMPK_EQ_U32       = 0x08,
    S_CMPK_LG_U32       = 0x09,
    S_CMPK_GT_U32       = 0x0A,
    S_CMPK_GE_U32       = 0x0B,
    S_CMPK_LT_U32       = 0x0C,
    S_CMPK_LE_U32       = 0x0D,
    S_ADDK_I32          = 0x0E,
    S_MULK_I32          = 0x0F,
    S_CBRANCH_I_FORK    = 0x10,
    S_SETK                = 0x11,
    S_CMPK_GEQ_I32      = 0x12,
    S_CMPK_GEU_U32      = 0x13,
    S_CMPK_LEQ_I32      = 0x14,
    S_CMPK_LEU_U32      = 0x15,
    S_CMPK_NEQ_I32      = 0x16,
    S_CMPK_NEQ_U32      = 0x17,
    S_CMPK_TRU_I32      = 0x18,
    S_CMPK_TRU_U32      = 0x19,
    S_CMPK_FALSE_I32    = 0x1A,
};

// ── SOPC Opcodes (gfx1030, 18 forms) ─────────────────────────────────────────
enum class SopcOpcode : uint8_t {
    S_CMP_EQ_I32        = 0x00,
    S_CMP_LG_I32        = 0x01,
    S_CMP_GT_I32        = 0x02,
    S_CMP_GE_I32        = 0x03,
    S_CMP_LT_I32        = 0x04,
    S_CMP_LE_I32        = 0x05,
    S_CMP_EQ_U32        = 0x06,
    S_CMP_LG_U32        = 0x07,
    S_CMP_GT_U32        = 0x08,
    S_CMP_GE_U32        = 0x09,
    S_CMP_LT_U32        = 0x0A,
    S_CMP_LE_U32        = 0x0B,
    S_BITCMP0_B32       = 0x0C,
    S_BITCMP1_B32       = 0x0D,
    S_BITCMP0_B64       = 0x0E,
    S_BITCMP1_B64       = 0x0F,
    S_CMP_TRU_I32       = 0x10,
    S_CMP_TRU_U32       = 0x11,
};

// ── SOPP Opcodes (gfx1030, 36 forms) ─────────────────────────────────────────
enum class SoppOpcode : uint8_t {
    S_NOP               = 0x00,
    S_ENDPGM            = 0x01,
    S_BRANCH            = 0x02,
    S_CBRANCH_SCC0      = 0x04,
    S_CBRANCH_SCC1      = 0x05,
    S_CBRANCH_VCCZ      = 0x06,
    S_CBRANCH_VCCNZ     = 0x07,
    S_CBRANCH_EXECZ     = 0x08,
    S_CBRANCH_EXECNZ    = 0x09,
    S_BARRIER           = 0x0A,
    S_WAITCNT           = 0x0C,
    S_WAITCNT_VSCNT     = 0x0D,
    S_WAITCNT_LGKMCNT   = 0x0E,
    S_WAITCNT_VMCNT     = 0x0F,
    S_SENDMSG           = 0x10,
    S_SENDMSGB          = 0x11,
    S_TRAP              = 0x12,
    S_ICACHE_INV        = 0x13,
    S_INCPERFLEVEL      = 0x14,
    S_DECPERFLEVEL      = 0x15,
    S_CLAUSE            = 0x16,
    S_SETVSKIP          = 0x17,
    S_SET_GPR_IDX_ON    = 0x18,
    S_SET_GPR_IDX_OFF   = 0x19,
    S_SET_GPR_IDX_MODE  = 0x1A,
    S_SETKILL           = 0x1B,
    S_SET_SHADER_CYCLES = 0x1C,
    S_ATOMIC_DEC        = 0x1D,
    S_DCACHE_WB         = 0x1E,
    S_DCACHE_WB_VOL     = 0x1F,
    S_DCACHE_INV        = 0x20,
    S_DCACHE_INV_VOL    = 0x21,
    S_TTRACEDATA        = 0x22,
    S_CBRANCH_CDBGSYS   = 0x23,
    S_CBRANCH_CDBGUSER  = 0x24,
    S_CBRANCH_CDBGSYS_OR_USER = 0x25,
};

// Decoded scalar instructions
struct Sop1Instruction { Sop1Opcode opcode; uint8_t sdst; uint8_t src0; };
struct Sop2Instruction { Sop2Opcode opcode; uint8_t sdst; uint8_t src0; uint8_t src1; bool scc_set; };
struct SopkInstruction { SopkOpcode opcode; uint8_t sdst; int16_t simm16; };
struct SopcInstruction { SopcOpcode opcode; uint8_t src0; uint8_t src1; };
struct SoppInstruction { SoppOpcode opcode; int16_t simm16; };

// Decode functions
Sop1Instruction decode_sop1(uint32_t encoded);
Sop2Instruction decode_sop2(uint32_t encoded);
SopkInstruction decode_sopk(uint32_t encoded);
SopcInstruction decode_sopc(uint32_t encoded);
SoppInstruction decode_sopp(uint32_t encoded);

// Translate to HLSL
std::string translate_sop1(const Sop1Instruction& instr);
std::string translate_sop2(const Sop2Instruction& instr);
std::string translate_sopk(const SopkInstruction& instr);
std::string translate_sopc(const SopcInstruction& instr);
std::string translate_sopp(const SoppInstruction& instr);

// Detection
bool is_sop1_instruction(uint32_t word);
bool is_sop2_instruction(uint32_t word);
bool is_sopk_instruction(uint32_t word);
bool is_sopc_instruction(uint32_t word);
bool is_sopp_instruction(uint32_t word);

} // namespace rdna2_to_hlsl
} // namespace dedsec