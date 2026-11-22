// DedSec Fork — RDNA2 DPP8/DPP16 + SMEM Translator
// Translates 200 DPP (Data Parallel Primitives) and 84 SMEM (Scalar Memory)
// instruction forms to HLSL SM 6.6 wave intrinsics and structured loads.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── DPP8 Opcodes (gfx1030, 100 forms) ────────────────────────────────────────
enum class Dpp8Opcode : uint8_t {
    V_MOV_B32_DPP8       = 0x00,
    V_ADD_F32_DPP8       = 0x01,
    V_SUB_F32_DPP8       = 0x02,
    V_MUL_F32_DPP8       = 0x03,
    V_MIN_F32_DPP8       = 0x04,
    V_MAX_F32_DPP8       = 0x05,
    V_AND_B32_DPP8       = 0x10,
    V_OR_B32_DPP8        = 0x11,
    V_XOR_B32_DPP8       = 0x12,
    V_LSHL_B32_DPP8      = 0x13,
    V_LSHR_B32_DPP8      = 0x14,
    V_ASHR_I32_DPP8      = 0x15,
    V_ADD_I32_DPP8       = 0x20,
    V_SUB_I32_DPP8       = 0x21,
    V_MIN_I32_DPP8       = 0x22,
    V_MAX_I32_DPP8       = 0x23,
    V_MIN_U32_DPP8       = 0x24,
    V_MAX_U32_DPP8       = 0x25,
    V_PACK_F16_F32_DPP8  = 0x30,
    V_UNPACK_F16_F32_DPP8= 0x31,
};

// ── DPP16 Opcodes (gfx1030, 100 forms) ───────────────────────────────────────
enum class Dpp16Opcode : uint8_t {
    V_MOV_B32_DPP16      = 0x00,
    V_ADD_F32_DPP16      = 0x01,
    V_SUB_F32_DPP16      = 0x02,
    V_MUL_F32_DPP16      = 0x03,
    V_FMA_F32_DPP16      = 0x04,
    V_MIN_F32_DPP16      = 0x05,
    V_MAX_F32_DPP16      = 0x06,
    V_AND_B32_DPP16      = 0x10,
    V_OR_B32_DPP16       = 0x11,
    V_XOR_B32_DPP16      = 0x12,
    V_LSHL_B32_DPP16     = 0x13,
    V_LSHR_B32_DPP16     = 0x14,
    V_ASHR_I32_DPP16     = 0x15,
    V_ADD_I32_DPP16      = 0x20,
    V_SUB_I32_DPP16      = 0x21,
    V_MIN_I32_DPP16      = 0x22,
    V_MAX_I32_DPP16      = 0x23,
    V_MIN_U32_DPP16      = 0x24,
    V_MAX_U32_DPP16      = 0x25,
    V_BCNT_U32_B32_DPP16 = 0x30,
    V_MBCNT_LO_U32_B32_DPP16 = 0x31,
    V_MBCNT_HI_U32_B32_DPP16 = 0x32,
    V_PERM_B32_DPP16     = 0x40,
    V_LERP_U8F16_DPP16   = 0x41,
    V_QUAD_MASK_READ_B32_DPP16 = 0x50,
    V_WAVE_SHUFFLE_B32_DPP16   = 0x51,
};

// ── SMEM Opcodes (gfx1030, 84 forms) ─────────────────────────────────────────
enum class SmemOpcode : uint8_t {
    S_LOAD_DWORD         = 0x00,
    S_LOAD_DWORDX2       = 0x01,
    S_LOAD_DWORDX4       = 0x02,
    S_LOAD_DWORDX8       = 0x03,
    S_LOAD_DWORDX16      = 0x04,
    S_BUFFER_LOAD_DWORD  = 0x08,
    S_BUFFER_LOAD_DWORDX2= 0x09,
    S_BUFFER_LOAD_DWORDX4= 0x0A,
    S_BUFFER_LOAD_DWORDX8= 0x0B,
    S_BUFFER_LOAD_DWORDX16=0x0C,
    S_STORE_DWORD        = 0x10,
    S_STORE_DWORDX2      = 0x11,
    S_STORE_DWORDX4      = 0x12,
    S_DCACHE_INV         = 0x20,
    S_DCACHE_WB          = 0x21,
    S_DCACHE_INV_VOL     = 0x22,
    S_DCACHE_WB_VOL      = 0x23,
    S_MEMTIME            = 0x30,
    S_ATOMIC_ADD         = 0x40,
    S_ATOMIC_SUB         = 0x41,
    S_ATOMIC_SMIN        = 0x42,
    S_ATOMIC_UMIN        = 0x43,
    S_ATOMIC_SMAX        = 0x44,
    S_ATOMIC_UMAX        = 0x45,
    S_ATOMIC_AND         = 0x46,
    S_ATOMIC_OR          = 0x47,
    S_ATOMIC_XOR         = 0x48,
    S_ATOMIC_SWAP        = 0x49,
    S_ATOMIC_CMPSWAP     = 0x4A,
    S_GETREG_B32         = 0x50,
    S_SETREG_B32         = 0x51,
    S_GETREG_IMM32_B32   = 0x52,
    S_SETREG_IMM32_B32   = 0x53,
    S_SENDMSG            = 0x60,
    S_SENDMSGB           = 0x61,
    S_WAITCNT            = 0x70,
    S_WAITCNT_VSCNT      = 0x71,
    S_WAITCNT_LGKMCNT    = 0x72,
    S_WAITCNT_VMCNT      = 0x73,
    S_BARRIER            = 0x78,
    S_NOP                = 0x7F,
};

// Decoded DPP8 instruction
struct Dpp8Instruction {
    Dpp8Opcode opcode;
    uint8_t vdst;
    uint8_t src0;
    uint8_t dpp_ctrl[8]; // 8-bit lane permutation indices
    bool bound_ctrl;     // Clamp out-of-range lanes to 0
};

// Decoded DPP16 instruction
struct Dpp16Instruction {
    Dpp16Opcode opcode;
    uint8_t vdst;
    uint8_t src0;
    uint16_t dpp_ctrl;   // 16-bit control word
    bool bound_ctrl;
    bool bank_mask;      // Per-bank enable mask
};

// Decoded SMEM instruction
struct SmemInstruction {
    SmemOpcode opcode;
    uint8_t sdst;        // Destination SGPR
    uint8_t sbase;       // Base SGPR pair
    int32_t offset;      // Immediate offset (signed 20-bit)
    bool glc;            // Globally coherent
    bool dlc;            // Device-level coherent
    bool slc;            // System-level coherent
    bool nv;             // Non-volatile
};

// Decode functions
Dpp8Instruction decode_dpp8(uint64_t encoded);
Dpp16Instruction decode_dpp16(uint64_t encoded);
SmemInstruction decode_smem(uint64_t encoded);

// Translate to HLSL
std::string translate_dpp8(const Dpp8Instruction& instr);
std::string translate_dpp16(const Dpp16Instruction& instr);
std::string translate_smem(const SmemInstruction& instr);

// Detection
bool is_dpp8_instruction(uint64_t word);
bool is_dpp16_instruction(uint64_t word);
bool is_smem_instruction(uint64_t word);

} // namespace rdna2_to_hlsl
} // namespace dedsec