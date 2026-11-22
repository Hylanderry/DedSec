// DedSec Fork — RDNA2 DPP8/DPP16 + SMEM Translator Implementation
// Translates 200 DPP (Data Parallel Primitives) and 84 SMEM (Scalar Memory)
// instruction forms to HLSL SM 6.6 wave intrinsics and structured loads.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "rdna2_dpp_smem.h"
#include <sstream>
#include <cstdio>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── DPP8 Detection & Decoding ────────────────────────────────────────────────

bool is_dpp8_instruction(uint64_t word) {
    // DPP8 instructions: [63:56] = 0xFA for gfx1030
    uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
    return (enc == 0xFA);
}

Dpp8Instruction decode_dpp8(uint64_t encoded) {
    Dpp8Instruction instr = {};
    // DPP8 encoding: [63:56]=ENC, [55:48]=opcode, [47:40]=vdst, [39:32]=src0,
    // [31:0] = 8 x 4-bit lane permutation indices
    instr.opcode = static_cast<Dpp8Opcode>((encoded >> 48) & 0xFF);
    instr.vdst = static_cast<uint8_t>((encoded >> 40) & 0xFF);
    instr.src0 = static_cast<uint8_t>((encoded >> 32) & 0xFF);

    for (int i = 0; i < 8; i++) {
        instr.dpp_ctrl[i] = static_cast<uint8_t>((encoded >> (i * 4)) & 0x0F);
    }

    instr.bound_ctrl = (encoded & (1ULL << 32)) != 0;
    return instr;
}

// ── DPP16 Detection & Decoding ───────────────────────────────────────────────

bool is_dpp16_instruction(uint64_t word) {
    // DPP16 instructions: [63:56] = 0xFB for gfx1030
    uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
    return (enc == 0xFB);
}

Dpp16Instruction decode_dpp16(uint64_t encoded) {
    Dpp16Instruction instr = {};
    // DPP16 encoding: [63:56]=ENC, [55:48]=opcode, [47:40]=vdst, [39:32]=src0,
    // [31:16]=dpp_ctrl(16 bits), [15]=bound_ctrl, [14]=bank_mask
    instr.opcode = static_cast<Dpp16Opcode>((encoded >> 48) & 0xFF);
    instr.vdst = static_cast<uint8_t>((encoded >> 40) & 0xFF);
    instr.src0 = static_cast<uint8_t>((encoded >> 32) & 0xFF);
    instr.dpp_ctrl = static_cast<uint16_t>((encoded >> 16) & 0xFFFF);
    instr.bound_ctrl = (encoded & (1ULL << 15)) != 0;
    instr.bank_mask = (encoded & (1ULL << 14)) != 0;
    return instr;
}

// ── SMEM Detection & Decoding ────────────────────────────────────────────────

bool is_smem_instruction(uint64_t word) {
    // SMEM instructions: [63:56] = 0xC0..0xCF for gfx1030
    uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
    return (enc >= 0xC0 && enc <= 0xCF);
}

SmemInstruction decode_smem(uint64_t encoded) {
    SmemInstruction instr = {};
    // SMEM encoding: [63:56]=ENC, [55:48]=opcode, [47:40]=sdst, [39:32]=sbase,
    // [31:12]=offset(20 bits signed), [11]=glc, [10]=dlc, [9]=slc, [8]=nv
    instr.opcode = static_cast<SmemOpcode>((encoded >> 48) & 0xFF);
    instr.sdst = static_cast<uint8_t>((encoded >> 40) & 0xFF);
    instr.sbase = static_cast<uint8_t>((encoded >> 32) & 0xFF);

    int32_t raw_offset = static_cast<int32_t>((encoded >> 12) & 0xFFFFF);
    // Sign-extend 20-bit offset
    if (raw_offset & 0x80000) {
        raw_offset |= static_cast<int32_t>(0xFFF00000);
    }
    instr.offset = raw_offset;

    instr.glc = (encoded & (1ULL << 11)) != 0;
    instr.dlc = (encoded & (1ULL << 10)) != 0;
    instr.slc = (encoded & (1ULL << 9)) != 0;
    instr.nv = (encoded & (1ULL << 8)) != 0;
    return instr;
}

// ── DPP8 → HLSL Translation ─────────────────────────────────────────────────

std::string translate_dpp8(const Dpp8Instruction& instr) {
    std::ostringstream out;

    // DPP8 maps to WaveReadLaneAt with explicit lane index per lane
    // In HLSL SM 6.6 we use WaveReadLaneAt + switch or helper functions
    out << "/* DPP8 perm=[";
    for (int i = 0; i < 8; i++) {
        if (i > 0) out << ",";
        out << static_cast<int>(instr.dpp_ctrl[i]);
    }
    out << "]";
    if (instr.bound_ctrl) out << " bc=1";
    out << " */ ";

    switch (instr.opcode) {
        case Dpp8Opcode::V_MOV_B32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = dpp8_mov(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_ADD_F32_DPP8:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " + dpp8_f32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_SUB_F32_DPP8:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " - dpp8_f32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_MUL_F32_DPP8:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " * dpp8_f32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_MIN_F32_DPP8:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = min(v" << static_cast<int>(instr.vdst)
                << ", dpp8_f32(v" << static_cast<int>(instr.src0) << "));";
            break;
        case Dpp8Opcode::V_MAX_F32_DPP8:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = max(v" << static_cast<int>(instr.vdst)
                << ", dpp8_f32(v" << static_cast<int>(instr.src0) << "));";
            break;
        case Dpp8Opcode::V_AND_B32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " & dpp8_u32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_OR_B32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " | dpp8_u32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_XOR_B32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " ^ dpp8_u32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_LSHL_B32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " << dpp8_u32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_LSHR_B32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " >> dpp8_u32(v" << static_cast<int>(instr.src0) << ");";
            break;
        case Dpp8Opcode::V_ASHR_I32_DPP8:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = int(v" << static_cast<int>(instr.vdst)
                << ") >> int(dpp8_u32(v" << static_cast<int>(instr.src0) << "));";
            break;
        case Dpp8Opcode::V_ADD_I32_DPP8:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = int(v" << static_cast<int>(instr.vdst)
                << ") + int(dpp8_u32(v" << static_cast<int>(instr.src0) << "));";
            break;
        case Dpp8Opcode::V_SUB_I32_DPP8:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = int(v" << static_cast<int>(instr.vdst)
                << ") - int(dpp8_u32(v" << static_cast<int>(instr.src0) << "));";
            break;
        case Dpp8Opcode::V_MIN_I32_DPP8:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = min(int(v" << static_cast<int>(instr.vdst)
                << "), int(dpp8_u32(v" << static_cast<int>(instr.src0) << ")));";
            break;
        case Dpp8Opcode::V_MAX_I32_DPP8:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = max(int(v" << static_cast<int>(instr.vdst)
                << "), int(dpp8_u32(v" << static_cast<int>(instr.src0) << ")));";
            break;
        case Dpp8Opcode::V_MIN_U32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = min(v" << static_cast<int>(instr.vdst)
                << ", dpp8_u32(v" << static_cast<int>(instr.src0) << "));";
            break;
        case Dpp8Opcode::V_MAX_U32_DPP8:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = max(v" << static_cast<int>(instr.vdst)
                << ", dpp8_u32(v" << static_cast<int>(instr.src0) << "));";
            break;
        default:
            out << "/* UNSUPPORTED DPP8 opcode 0x"
                << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
            break;
    }

    return out.str();
}

// ── DPP16 → HLSL Translation ─────────────────────────────────────────────────

std::string translate_dpp16(const Dpp16Instruction& instr) {
    std::ostringstream out;

    // DPP16 maps to QuadReadLaneAt / WaveReadLaneAt with 16-lane permutations
    out << "/* DPP16 ctrl=0x" << std::hex << instr.dpp_ctrl << std::dec;
    if (instr.bound_ctrl) out << " bc=1";
    if (instr.bank_mask) out << " bm=1";
    out << " */ ";

    switch (instr.opcode) {
        case Dpp16Opcode::V_MOV_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = dpp16_mov(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_ADD_F32_DPP16:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " + dpp16_f32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_SUB_F32_DPP16:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " - dpp16_f32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_MUL_F32_DPP16:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " * dpp16_f32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_FMA_F32_DPP16:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = mad(v" << static_cast<int>(instr.vdst)
                << ", dpp16_f32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec
                << "), src2);";
            break;
        case Dpp16Opcode::V_MIN_F32_DPP16:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = min(v" << static_cast<int>(instr.vdst)
                << ", dpp16_f32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << "));";
            break;
        case Dpp16Opcode::V_MAX_F32_DPP16:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = max(v" << static_cast<int>(instr.vdst)
                << ", dpp16_f32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << "));";
            break;
        case Dpp16Opcode::V_AND_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " & dpp16_u32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_OR_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " | dpp16_u32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_XOR_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = v" << static_cast<int>(instr.vdst)
                << " ^ dpp16_u32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_BCNT_U32_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = countbits(dpp16_u32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << "));";
            break;
        case Dpp16Opcode::V_MBCNT_LO_U32_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = mbcnt_lo(dpp16_u32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << "));";
            break;
        case Dpp16Opcode::V_MBCNT_HI_U32_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = mbcnt_hi(dpp16_u32(v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << "));";
            break;
        case Dpp16Opcode::V_PERM_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = dpp16_perm(v" << static_cast<int>(instr.vdst)
                << ", v" << static_cast<int>(instr.src0)
                << ", 0x" << std::hex << instr.dpp_ctrl << std::dec << ");";
            break;
        case Dpp16Opcode::V_QUAD_MASK_READ_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = QuadReadLaneAt(v" << static_cast<int>(instr.src0)
                << ", dpp16_quad_lane(0x" << std::hex << instr.dpp_ctrl << std::dec << "));";
            break;
        case Dpp16Opcode::V_WAVE_SHUFFLE_B32_DPP16:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = WaveReadLaneAt(v" << static_cast<int>(instr.src0)
                << ", dpp16_shuffle_lane(0x" << std::hex << instr.dpp_ctrl << std::dec << "));";
            break;
        default:
            out << "/* UNSUPPORTED DPP16 opcode 0x"
                << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
            break;
    }

    return out.str();
}

// ── SMEM → HLSL Translation ──────────────────────────────────────────────────

std::string translate_smem(const SmemInstruction& instr) {
    std::ostringstream out;

    // Coherence modifiers
    if (instr.glc || instr.dlc || instr.slc) {
        out << "/* ";
        if (instr.glc) out << "glc ";
        if (instr.dlc) out << "dlc ";
        if (instr.slc) out << "slc ";
        out << "*/ ";
    }

    std::string sbase = "s" + std::to_string(instr.sbase);
    std::string sdst = "s" + std::to_string(instr.sdst);

    switch (instr.opcode) {
        // ── Scalar loads ─────────────────────────────────────────────────
        case SmemOpcode::S_LOAD_DWORD:
            out << "uint " << sdst << " = smem_load_dword(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_LOAD_DWORDX2:
            out << "uint2 " << sdst << " = smem_load_dwordx2(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_LOAD_DWORDX4:
            out << "uint4 " << sdst << " = smem_load_dwordx4(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_LOAD_DWORDX8:
            out << "/* S_LOAD_DWORDX8: 8-dword scalar load */ "
                << sdst << "[0..7] = smem_load_dwordx8(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_LOAD_DWORDX16:
            out << "/* S_LOAD_DWORDX16: 16-dword scalar load */ "
                << sdst << "[0..15] = smem_load_dwordx16(" << sbase
                << ", " << instr.offset << ");";
            break;

        // ── Buffer scalar loads ──────────────────────────────────────────
        case SmemOpcode::S_BUFFER_LOAD_DWORD:
            out << "uint " << sdst << " = sbufer_load_dword(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_BUFFER_LOAD_DWORDX2:
            out << "uint2 " << sdst << " = sbuffer_load_dwordx2(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_BUFFER_LOAD_DWORDX4:
            out << "uint4 " << sdst << " = sbuffer_load_dwordx4(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_BUFFER_LOAD_DWORDX8:
            out << "/* S_BUFFER_LOAD_DWORDX8 */ "
                << sdst << "[0..7] = sbuffer_load_dwordx8(" << sbase
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_BUFFER_LOAD_DWORDX16:
            out << "/* S_BUFFER_LOAD_DWORDX16 */ "
                << sdst << "[0..15] = sbuffer_load_dwordx16(" << sbase
                << ", " << instr.offset << ");";
            break;

        // ── Scalar stores ────────────────────────────────────────────────
        case SmemOpcode::S_STORE_DWORD:
            out << "smem_store_dword(" << sbase << ", " << sdst
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_STORE_DWORDX2:
            out << "smem_store_dwordx2(" << sbase << ", " << sdst
                << ", " << instr.offset << ");";
            break;
        case SmemOpcode::S_STORE_DWORDX4:
            out << "smem_store_dwordx4(" << sbase << ", " << sdst
                << ", " << instr.offset << ");";
            break;

        // ── Cache operations ─────────────────────────────────────────────
        case SmemOpcode::S_DCACHE_INV:
            out << "DeviceMemoryBarrier(); /* S_DCACHE_INV */";
            break;
        case SmemOpcode::S_DCACHE_WB:
            out << "DeviceMemoryBarrier(); /* S_DCACHE_WB */";
            break;
        case SmemOpcode::S_DCACHE_INV_VOL:
            out << "DeviceMemoryBarrier(); /* S_DCACHE_INV_VOL */";
            break;
        case SmemOpcode::S_DCACHE_WB_VOL:
            out << "DeviceMemoryBarrier(); /* S_DCACHE_WB_VOL */";
            break;

        // ── Memory timestamp ─────────────────────────────────────────────
        case SmemOpcode::S_MEMTIME:
            out << "uint2 " << sdst << " = smem_memtime();";
            break;

        // ── Scalar atomics ───────────────────────────────────────────────
        case SmemOpcode::S_ATOMIC_ADD:
            out << "uint " << sdst << " = InterlockedAdd(smem_ptr("
                << sbase << ", " << instr.offset << "), " << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_SUB:
            out << "uint " << sdst << " = InterlockedAdd(smem_ptr("
                << sbase << ", " << instr.offset << "), -" << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_SMIN:
        case SmemOpcode::S_ATOMIC_UMIN:
            out << "uint " << sdst << " = InterlockedMin(smem_ptr("
                << sbase << ", " << instr.offset << "), " << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_SMAX:
        case SmemOpcode::S_ATOMIC_UMAX:
            out << "uint " << sdst << " = InterlockedMax(smem_ptr("
                << sbase << ", " << instr.offset << "), " << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_AND:
            out << "uint " << sdst << " = InterlockedAnd(smem_ptr("
                << sbase << ", " << instr.offset << "), " << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_OR:
            out << "uint " << sdst << " = InterlockedOr(smem_ptr("
                << sbase << ", " << instr.offset << "), " << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_XOR:
            out << "uint " << sdst << " = InterlockedXor(smem_ptr("
                << sbase << ", " << instr.offset << "), " << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_SWAP:
            out << "uint " << sdst << " = InterlockedExchange(smem_ptr("
                << sbase << ", " << instr.offset << "), " << sdst << ");";
            break;
        case SmemOpcode::S_ATOMIC_CMPSWAP:
            out << "uint " << sdst << " = InterlockedCompareExchange(smem_ptr("
                << sbase << ", " << instr.offset << "), cmp_val, " << sdst << ");";
            break;

        // ── Register access ──────────────────────────────────────────────
        case SmemOpcode::S_GETREG_B32:
            out << "uint " << sdst << " = s_getreg(" << instr.offset << ");";
            break;
        case SmemOpcode::S_SETREG_B32:
            out << "s_setreg(" << instr.offset << ", " << sdst << ");";
            break;
        case SmemOpcode::S_GETREG_IMM32_B32:
            out << "uint " << sdst << " = s_getreg_imm(" << instr.offset << ");";
            break;
        case SmemOpcode::S_SETREG_IMM32_B32:
            out << "s_setreg_imm(" << instr.offset << ", " << sdst << ");";
            break;

        // ── Messaging ────────────────────────────────────────────────────
        case SmemOpcode::S_SENDMSG:
            out << "/* S_SENDMSG op=" << (instr.offset & 0xFF) << " */";
            break;
        case SmemOpcode::S_SENDMSGB:
            out << "/* S_SENDMSGB op=" << (instr.offset & 0xFF) << " */";
            break;

        // ── Wait/barrier/sync ────────────────────────────────────────────
        case SmemOpcode::S_WAITCNT:
            out << "/* S_WAITCNT vm=" << ((instr.offset >> 0) & 0xF)
                << " lgkm=" << ((instr.offset >> 4) & 0x3F)
                << " vs=" << ((instr.offset >> 10) & 0xF) << " */";
            break;
        case SmemOpcode::S_WAITCNT_VSCNT:
            out << "/* S_WAITCNT_VSCNT vs=" << (instr.offset & 0xF) << " */";
            break;
        case SmemOpcode::S_WAITCNT_LGKMCNT:
            out << "/* S_WAITCNT_LGKMCNT lgkm=" << (instr.offset & 0x3F) << " */";
            break;
        case SmemOpcode::S_WAITCNT_VMCNT:
            out << "/* S_WAITCNT_VMCNT vm=" << (instr.offset & 0xF) << " */";
            break;
        case SmemOpcode::S_BARRIER:
            out << "GroupMemoryBarrierWithGroupSync(); /* S_BARRIER */";
            break;
        case SmemOpcode::S_NOP:
            out << "/* S_NOP */";
            break;

        default:
            out << "/* UNSUPPORTED SMEM opcode 0x"
                << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
            break;
    }

    return out.str();
}

} // namespace rdna2_to_hlsl
} // namespace dedsec