// DedSec Fork — RDNA2 SOP (Scalar ALU) Translator Implementation
// Translates SOP1(65) + SOP2(51) + SOPK(27) + SOPC(18) + SOPP(36) = 197 scalar
// instruction forms to HLSL SM 6.6 compute shader equivalents.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "rdna2_sop.h"
#include <sstream>
#include <cstdio>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── Detection ────────────────────────────────────────────────────────────────

bool is_sop1_instruction(uint32_t word) {
    // SOP1: [31:23] = 0x1FD for gfx1030
    return ((word >> 23) & 0x1FF) == 0x1FD;
}

bool is_sop2_instruction(uint32_t word) {
    // SOP2: bit31=1, bit30=0 → [31:30] = 0b10
    return ((word >> 30) == 2u);
}

bool is_sopk_instruction(uint32_t word) {
    // SOPK: [31:28] = 0xB for gfx1030
    return ((word >> 28) & 0xF) == 0xB;
}

bool is_sopc_instruction(uint32_t word) {
    // SOPC: [31:23] = 0x1FE for gfx1030
    return ((word >> 23) & 0x1FF) == 0x1FE;
}

bool is_sopp_instruction(uint32_t word) {
    // SOPP: [31:23] = 0x1FF for gfx1030
    return ((word >> 23) & 0x1FF) == 0x1FF;
}

// ── Decoding ─────────────────────────────────────────────────────────────────

Sop1Instruction decode_sop1(uint32_t encoded) {
    Sop1Instruction instr = {};
    // SOP1: [31:23]=0x1FD, [22:16]=opcode(7 bits), [15:8]=sdst, [7:0]=src0
    instr.opcode = static_cast<Sop1Opcode>((encoded >> 16) & 0x7F);
    instr.sdst = static_cast<uint8_t>((encoded >> 8) & 0xFF);
    instr.src0 = static_cast<uint8_t>(encoded & 0xFF);
    return instr;
}

Sop2Instruction decode_sop2(uint32_t encoded) {
    Sop2Instruction instr = {};
    // SOP2: [31]=1,[30]=0,[29:23]=opcode(7 bits),[22:16]=sdst,[15:8]=src0,[7:0]=src1
    instr.opcode = static_cast<Sop2Opcode>((encoded >> 23) & 0x7F);
    instr.sdst = static_cast<uint8_t>((encoded >> 16) & 0x7F);
    instr.src0 = static_cast<uint8_t>((encoded >> 8) & 0xFF);
    instr.src1 = static_cast<uint8_t>(encoded & 0xFF);
    instr.scc_set = true; // Most SOP2 ops set SCC
    return instr;
}

SopkInstruction decode_sopk(uint32_t encoded) {
    SopkInstruction instr = {};
    // SOPK: [31:28]=0xB, [27:23]=opcode(5 bits), [22:16]=sdst, [15:0]=simm16
    instr.opcode = static_cast<SopkOpcode>((encoded >> 23) & 0x1F);
    instr.sdst = static_cast<uint8_t>((encoded >> 16) & 0x7F);
    instr.simm16 = static_cast<int16_t>(encoded & 0xFFFF);
    return instr;
}

SopcInstruction decode_sopc(uint32_t encoded) {
    SopcInstruction instr = {};
    // SOPC: [31:23]=0x1FE, [22:16]=opcode(7 bits), [15:8]=src0, [7:0]=src1
    instr.opcode = static_cast<SopcOpcode>((encoded >> 16) & 0x7F);
    instr.src0 = static_cast<uint8_t>((encoded >> 8) & 0xFF);
    instr.src1 = static_cast<uint8_t>(encoded & 0xFF);
    return instr;
}

SoppInstruction decode_sopp(uint32_t encoded) {
    SoppInstruction instr = {};
    // SOPP: [31:23]=0x1FF, [22:16]=opcode(7 bits), [15:0]=simm16
    instr.opcode = static_cast<SoppOpcode>((encoded >> 16) & 0x7F);
    instr.simm16 = static_cast<int16_t>(encoded & 0xFFFF);
    return instr;
}

// ── SOP1 → HLSL ─────────────────────────────────────────────────────────────

std::string translate_sop1(const Sop1Instruction& instr) {
    std::ostringstream out;
    int sd = static_cast<int>(instr.sdst);
    int s0 = static_cast<int>(instr.src0);

    switch (instr.opcode) {
    case Sop1Opcode::S_MOV_B32:
        out << "uint s" << sd << " = s" << s0 << ";";
        break;
    case Sop1Opcode::S_MOV_B64:
        out << "uint2 s" << sd << " = s" << s0 << ";";
        break;
    case Sop1Opcode::S_CMOV_B32:
        out << "s" << sd << " = scc ? s" << s0 << " : s" << sd << ";";
        break;
    case Sop1Opcode::S_CMOV_B64:
        out << "s" << sd << " = scc ? s" << s0 << " : s" << sd << ";";
        break;
    case Sop1Opcode::S_NOT_B32:
        out << "uint s" << sd << " = ~s" << s0 << "; scc = (s" << sd << " != 0);";
        break;
    case Sop1Opcode::S_NOT_B64:
        out << "uint2 s" << sd << " = ~s" << s0 << "; scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop1Opcode::S_WQM_B32:
        out << "uint s" << sd << " = wqm(s" << s0 << ");";
        break;
    case Sop1Opcode::S_WQM_B64:
        out << "uint2 s" << sd << " = wqm64(s" << s0 << ");";
        break;
    case Sop1Opcode::S_BREV_B32:
        out << "uint s" << sd << " = reversebits(s" << s0 << "); scc = (s" << sd << " != 0);";
        break;
    case Sop1Opcode::S_BREV_B64:
        out << "uint2 s" << sd << " = brev64(s" << s0 << "); scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop1Opcode::S_BCNT0_I32_B32:
        out << "int s" << sd << " = countbits(~s" << s0 << ");";
        break;
    case Sop1Opcode::S_BCNT0_I32_B64:
        out << "int s" << sd << " = countbits(~s" << s0 << ".x) + countbits(~s" << s0 << ".y);";
        break;
    case Sop1Opcode::S_BCNT1_I32_B32:
        out << "int s" << sd << " = countbits(s" << s0 << ");";
        break;
    case Sop1Opcode::S_BCNT1_I32_B64:
        out << "int s" << sd << " = countbits(s" << s0 << ".x) + countbits(s" << s0 << ".y);";
        break;
    case Sop1Opcode::S_FF0_I32_B32:
        out << "int s" << sd << " = firstbitlow(~s" << s0 << ");";
        break;
    case Sop1Opcode::S_FF0_I32_B64:
        out << "int s" << sd << " = ff0_i64(~s" << s0 << ");";
        break;
    case Sop1Opcode::S_FF1_I32_B32:
        out << "int s" << sd << " = firstbitlow(s" << s0 << ");";
        break;
    case Sop1Opcode::S_FF1_I32_B64:
        out << "int s" << sd << " = ff1_i64(s" << s0 << ");";
        break;
    case Sop1Opcode::S_FLBIT_I32_B32:
        out << "int s" << sd << " = firstbithigh(s" << s0 << ");";
        break;
    case Sop1Opcode::S_FLBIT_I32_B64:
        out << "int s" << sd << " = flbit_i64(s" << s0 << ");";
        break;
    case Sop1Opcode::S_FLBIT_I32:
        out << "int s" << sd << " = firstbithigh(int(s" << s0 << "));";
        break;
    case Sop1Opcode::S_FLBIT_I32_I64:
        out << "int s" << sd << " = flbit_i64_signed(s" << s0 << ");";
        break;
    case Sop1Opcode::S_SEXT_I32_I8:
        out << "int s" << sd << " = int((int8_t)(s" << s0 << " & 0xFFu));";
        break;
    case Sop1Opcode::S_SEXT_I32_I16:
        out << "int s" << sd << " = int((int16_t)(s" << s0 << " & 0xFFFFu));";
        break;
    case Sop1Opcode::S_BITSET0_B32:
        out << "uint s" << sd << " = s" << s0 << " & ~(1u << bit_pos);";
        break;
    case Sop1Opcode::S_BITSET0_B64:
        out << "uint2 s" << sd << " = bitset0_64(s" << s0 << ", bit_pos);";
        break;
    case Sop1Opcode::S_BITSET1_B32:
        out << "uint s" << sd << " = s" << s0 << " | (1u << bit_pos);";
        break;
    case Sop1Opcode::S_BITSET1_B64:
        out << "uint2 s" << sd << " = bitset1_64(s" << s0 << ", bit_pos);";
        break;
    case Sop1Opcode::S_GETPC_B64:
        out << "uint2 s" << sd << " = get_pc(); /* program counter */";
        break;
    case Sop1Opcode::S_SETPC_B64:
        out << "/* S_SETPC_B64: set program counter to s" << s0 << " */";
        break;
    case Sop1Opcode::S_SWAPPC_B64:
        out << "uint2 tmp_pc = s" << sd << "; s" << sd << " = s" << s0 << "; /* swap PC */";
        break;
    case Sop1Opcode::S_RFE_B64:
        out << "/* S_RFE_B64: return from exception */";
        break;
    case Sop1Opcode::S_AND_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask &= s" << s0 << "; s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_OR_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask |= s" << s0 << "; s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_XOR_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask ^= s" << s0 << "; s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_ANDN2_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask &= ~s" << s0 << "; s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_ORN2_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask |= ~s" << s0 << "; s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_NAND_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask = ~(exec_mask & s" << s0 << "); s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_NOR_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask = ~(exec_mask | s" << s0 << "); s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_XNOR_SAVEEXEC_B64:
        out << "uint2 saved_exec = exec_mask; exec_mask = ~(exec_mask ^ s" << s0 << "); s" << sd << " = saved_exec;";
        break;
    case Sop1Opcode::S_QUADMASK_B32:
        out << "uint s" << sd << " = quadmask(s" << s0 << ");";
        break;
    case Sop1Opcode::S_QUADMASK_B64:
        out << "uint2 s" << sd << " = quadmask64(s" << s0 << ");";
        break;
    case Sop1Opcode::S_MOVRELS_B32:
        out << "uint s" << sd << " = sgpr_rel[s" << s0 << "];";
        break;
    case Sop1Opcode::S_MOVRELS_B64:
        out << "uint2 s" << sd << " = sgpr_rel64[s" << s0 << "];";
        break;
    case Sop1Opcode::S_MOVRELSD_B32:
        out << "sgpr_rel[s" << sd << "] = s" << s0 << ";";
        break;
    case Sop1Opcode::S_MOVRELSD_B64:
        out << "sgpr_rel64[s" << sd << "] = s" << s0 << ";";
        break;
    case Sop1Opcode::S_ABS_I32:
        out << "int s" << sd << " = abs(int(s" << s0 << ")); scc = (s" << sd << " != 0);";
        break;
    case Sop1Opcode::S_NEG_I32:
        out << "int s" << sd << " = -int(s" << s0 << "); scc = (s" << sd << " != 0);";
        break;
    case Sop1Opcode::S_NOP:
        out << "/* S_NOP */";
        break;
    default:
        out << "/* UNSUPPORTED SOP1 opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }
    return out.str();
}

// ── SOP2 → HLSL ─────────────────────────────────────────────────────────────

std::string translate_sop2(const Sop2Instruction& instr) {
    std::ostringstream out;
    int sd = static_cast<int>(instr.sdst);
    int s0 = static_cast<int>(instr.src0);
    int s1 = static_cast<int>(instr.src1);

    switch (instr.opcode) {
    case Sop2Opcode::S_ADD_U32:
        out << "uint s" << sd << " = s" << s0 << " + s" << s1 << "; scc = carry;";
        break;
    case Sop2Opcode::S_SUB_U32:
        out << "uint s" << sd << " = s" << s0 << " - s" << s1 << "; scc = borrow;";
        break;
    case Sop2Opcode::S_ADD_I32:
        out << "int s" << sd << " = int(s" << s0 << ") + int(s" << s1 << "); scc = overflow;";
        break;
    case Sop2Opcode::S_SUB_I32:
        out << "int s" << sd << " = int(s" << s0 << ") - int(s" << s1 << "); scc = overflow;";
        break;
    case Sop2Opcode::S_ADDC_U32:
        out << "uint s" << sd << " = s" << s0 << " + s" << s1 << " + (scc ? 1u : 0u);";
        break;
    case Sop2Opcode::S_SUBB_U32:
        out << "uint s" << sd << " = s" << s0 << " - s" << s1 << " - (scc ? 1u : 0u);";
        break;
    case Sop2Opcode::S_MIN_I32:
        out << "int s" << sd << " = min(int(s" << s0 << "), int(s" << s1 << ")); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_MIN_U32:
        out << "uint s" << sd << " = min(s" << s0 << ", s" << s1 << "); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_MAX_I32:
        out << "int s" << sd << " = max(int(s" << s0 << "), int(s" << s1 << ")); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_MAX_U32:
        out << "uint s" << sd << " = max(s" << s0 << ", s" << s1 << "); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_CSELECT_B32:
        out << "uint s" << sd << " = scc ? s" << s0 << " : s" << s1 << ";";
        break;
    case Sop2Opcode::S_CSELECT_B64:
        out << "uint2 s" << sd << " = scc ? s" << s0 << " : s" << s1 << ";";
        break;
    case Sop2Opcode::S_AND_B32:
        out << "uint s" << sd << " = s" << s0 << " & s" << s1 << "; scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_AND_B64:
        out << "uint2 s" << sd << " = s" << s0 << " & s" << s1 << "; scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_OR_B32:
        out << "uint s" << sd << " = s" << s0 << " | s" << s1 << "; scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_OR_B64:
        out << "uint2 s" << sd << " = s" << s0 << " | s" << s1 << "; scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_XOR_B32:
        out << "uint s" << sd << " = s" << s0 << " ^ s" << s1 << "; scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_XOR_B64:
        out << "uint2 s" << sd << " = s" << s0 << " ^ s" << s1 << "; scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_ANDN2_B32:
        out << "uint s" << sd << " = s" << s0 << " & ~s" << s1 << "; scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_ANDN2_B64:
        out << "uint2 s" << sd << " = s" << s0 << " & ~s" << s1 << "; scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_ORN2_B32:
        out << "uint s" << sd << " = s" << s0 << " | ~s" << s1 << "; scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_ORN2_B64:
        out << "uint2 s" << sd << " = s" << s0 << " | ~s" << s1 << "; scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_NAND_B32:
        out << "uint s" << sd << " = ~(s" << s0 << " & s" << s1 << "); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_NAND_B64:
        out << "uint2 s" << sd << " = ~(s" << s0 << " & s" << s1 << "); scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_NOR_B32:
        out << "uint s" << sd << " = ~(s" << s0 << " | s" << s1 << "); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_NOR_B64:
        out << "uint2 s" << sd << " = ~(s" << s0 << " | s" << s1 << "); scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_XNOR_B32:
        out << "uint s" << sd << " = ~(s" << s0 << " ^ s" << s1 << "); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_XNOR_B64:
        out << "uint2 s" << sd << " = ~(s" << s0 << " ^ s" << s1 << "); scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_LSHL_B32:
        out << "uint s" << sd << " = s" << s0 << " << (s" << s1 << " & 31u); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_LSHL_B64:
        out << "uint2 s" << sd << " = lshl64(s" << s0 << ", s" << s1 << "); scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_LSHR_B32:
        out << "uint s" << sd << " = s" << s0 << " >> (s" << s1 << " & 31u); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_LSHR_B64:
        out << "uint2 s" << sd << " = lshr64(s" << s0 << ", s" << s1 << "); scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_ASHR_I32:
        out << "int s" << sd << " = int(s" << s0 << ") >> int(s" << s1 << " & 31u); scc = (s" << sd << " != 0);";
        break;
    case Sop2Opcode::S_ASHR_I64:
        out << "uint2 s" << sd << " = ashr64(s" << s0 << ", s" << s1 << "); scc = (s" << sd << ".x != 0 || s" << sd << ".y != 0);";
        break;
    case Sop2Opcode::S_BFM_B32:
        out << "uint s" << sd << " = ((1u << (s" << s0 << " & 31u)) - 1u) << (s" << s1 << " & 31u);";
        break;
    case Sop2Opcode::S_BFM_B64:
        out << "uint2 s" << sd << " = bfm64(s" << s0 << ", s" << s1 << ");";
        break;
    case Sop2Opcode::S_MUL_I32:
        out << "int s" << sd << " = int(s" << s0 << ") * int(s" << s1 << ");";
        break;
    case Sop2Opcode::S_BFE_U32:
        out << "uint s" << sd << " = (s" << s0 << " >> (s" << s1 << " & 31u)) & ((1u << (width & 31u)) - 1u);";
        break;
    case Sop2Opcode::S_BFE_I32:
        out << "int s" << sd << " = bfe_i32(int(s" << s0 << "), int(s" << s1 << "), width);";
        break;
    case Sop2Opcode::S_BFE_U64:
        out << "uint2 s" << sd << " = bfe_u64(s" << s0 << ", s" << s1 << ", width);";
        break;
    case Sop2Opcode::S_BFE_I64:
        out << "uint2 s" << sd << " = bfe_i64(s" << s0 << ", s" << s1 << ", width);";
        break;
    case Sop2Opcode::S_BFI_B32:
        out << "uint s" << sd << " = (s" << s0 << " & ~mask) | (s" << s1 << " & mask);";
        break;
    case Sop2Opcode::S_BFI_B64:
        out << "uint2 s" << sd << " = bfi64(s" << s0 << ", s" << s1 << ", mask);";
        break;
    case Sop2Opcode::S_PACK_F16_F32:
        out << "uint s" << sd << " = f32tof16(asfloat(s" << s0 << ")) | (f32tof16(asfloat(s" << s1 << ")) << 16u);";
        break;
    case Sop2Opcode::S_PACK_F32_F16:
        out << "float s" << sd << " = f16tof32(s" << s0 << ");";
        break;
    case Sop2Opcode::S_WSHL_B32:
        out << "uint s" << sd << " = wshl_b32(s" << s0 << ", s" << s1 << ");";
        break;
    case Sop2Opcode::S_WSHL_B64:
        out << "uint2 s" << sd << " = wshl_b64(s" << s0 << ", s" << s1 << ");";
        break;
    case Sop2Opcode::S_WRSHL_B32:
        out << "uint s" << sd << " = wrshl_b32(s" << s0 << ", s" << s1 << ");";
        break;
    case Sop2Opcode::S_WRSHL_B64:
        out << "uint2 s" << sd << " = wrshl_b64(s" << s0 << ", s" << s1 << ");";
        break;
    default:
        out << "/* UNSUPPORTED SOP2 opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }
    return out.str();
}

// ── SOPK → HLSL ─────────────────────────────────────────────────────────────

std::string translate_sopk(const SopkInstruction& instr) {
    std::ostringstream out;
    int sd = static_cast<int>(instr.sdst);
    int imm = static_cast<int>(instr.simm16);

    switch (instr.opcode) {
    case SopkOpcode::S_MOVK_I32:
        out << "int s" << sd << " = " << imm << ";";
        break;
    case SopkOpcode::S_CMOVK_I32:
        out << "s" << sd << " = scc ? " << imm << " : s" << sd << ";";
        break;
    case SopkOpcode::S_CMPK_EQ_I32:
        out << "scc = (int(s" << sd << ") == " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_LG_I32:
        out << "scc = (int(s" << sd << ") != " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_GT_I32:
        out << "scc = (int(s" << sd << ") > " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_GE_I32:
        out << "scc = (int(s" << sd << ") >= " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_LT_I32:
        out << "scc = (int(s" << sd << ") < " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_LE_I32:
        out << "scc = (int(s" << sd << ") <= " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_EQ_U32:
        out << "scc = (s" << sd << " == " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_LG_U32:
        out << "scc = (s" << sd << " != " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_GT_U32:
        out << "scc = (s" << sd << " > " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_GE_U32:
        out << "scc = (s" << sd << " >= " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_LT_U32:
        out << "scc = (s" << sd << " < " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_LE_U32:
        out << "scc = (s" << sd << " <= " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_ADDK_I32:
        out << "int s" << sd << " = int(s" << sd << ") + " << imm << "; scc = (s" << sd << " != 0);";
        break;
    case SopkOpcode::S_MULK_I32:
        out << "int s" << sd << " = int(s" << sd << ") * " << imm << "; scc = (s" << sd << " != 0);";
        break;
    case SopkOpcode::S_CBRANCH_I_FORK:
        out << "/* S_CBRANCH_I_FORK simm=" << imm << " */";
        break;
    case SopkOpcode::S_SETK:
        out << "/* S_SETK k=" << imm << " */";
        break;
    case SopkOpcode::S_CMPK_GEQ_I32:
        out << "scc = (int(s" << sd << ") >= " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_GEU_U32:
        out << "scc = (s" << sd << " >= " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_LEQ_I32:
        out << "scc = (int(s" << sd << ") <= " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_LEU_U32:
        out << "scc = (s" << sd << " <= " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_NEQ_I32:
        out << "scc = (int(s" << sd << ") != " << imm << ");";
        break;
    case SopkOpcode::S_CMPK_NEQ_U32:
        out << "scc = (s" << sd << " != " << static_cast<uint32_t>(imm) << "u);";
        break;
    case SopkOpcode::S_CMPK_TRU_I32:
        out << "scc = true; /* always true */";
        break;
    case SopkOpcode::S_CMPK_TRU_U32:
        out << "scc = true; /* always true */";
        break;
    case SopkOpcode::S_CMPK_FALSE_I32:
        out << "scc = false; /* always false */";
        break;
    default:
        out << "/* UNSUPPORTED SOPK opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }
    return out.str();
}

// ── SOPC → HLSL ─────────────────────────────────────────────────────────────

std::string translate_sopc(const SopcInstruction& instr) {
    std::ostringstream out;
    int s0 = static_cast<int>(instr.src0);
    int s1 = static_cast<int>(instr.src1);

    switch (instr.opcode) {
    case SopcOpcode::S_CMP_EQ_I32:  out << "scc = (int(s" << s0 << ") == int(s" << s1 << "));"; break;
    case SopcOpcode::S_CMP_LG_I32:  out << "scc = (int(s" << s0 << ") != int(s" << s1 << "));"; break;
    case SopcOpcode::S_CMP_GT_I32:  out << "scc = (int(s" << s0 << ") > int(s" << s1 << "));"; break;
    case SopcOpcode::S_CMP_GE_I32:  out << "scc = (int(s" << s0 << ") >= int(s" << s1 << "));"; break;
    case SopcOpcode::S_CMP_LT_I32:  out << "scc = (int(s" << s0 << ") < int(s" << s1 << "));"; break;
    case SopcOpcode::S_CMP_LE_I32:  out << "scc = (int(s" << s0 << ") <= int(s" << s1 << "));"; break;
    case SopcOpcode::S_CMP_EQ_U32:  out << "scc = (s" << s0 << " == s" << s1 << ");"; break;
    case SopcOpcode::S_CMP_LG_U32:  out << "scc = (s" << s0 << " != s" << s1 << ");"; break;
    case SopcOpcode::S_CMP_GT_U32:  out << "scc = (s" << s0 << " > s" << s1 << ");"; break;
    case SopcOpcode::S_CMP_GE_U32:  out << "scc = (s" << s0 << " >= s" << s1 << ");"; break;
    case SopcOpcode::S_CMP_LT_U32:  out << "scc = (s" << s0 << " < s" << s1 << ");"; break;
    case SopcOpcode::S_CMP_LE_U32:  out << "scc = (s" << s0 << " <= s" << s1 << ");"; break;
    case SopcOpcode::S_BITCMP0_B32: out << "scc = ((s" << s0 << " >> (s" << s1 << " & 31u)) & 1u) == 0;"; break;
    case SopcOpcode::S_BITCMP1_B32: out << "scc = ((s" << s0 << " >> (s" << s1 << " & 31u)) & 1u) == 1;"; break;
    case SopcOpcode::S_BITCMP0_B64: out << "scc = bitcmp0_64(s" << s0 << ", s" << s1 << ");"; break;
    case SopcOpcode::S_BITCMP1_B64: out << "scc = bitcmp1_64(s" << s0 << ", s" << s1 << ");"; break;
    case SopcOpcode::S_CMP_TRU_I32: out << "scc = true;"; break;
    case SopcOpcode::S_CMP_TRU_U32: out << "scc = true;"; break;
    default:
        out << "/* UNSUPPORTED SOPC opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }
    return out.str();
}

// ── SOPP → HLSL ─────────────────────────────────────────────────────────────

std::string translate_sopp(const SoppInstruction& instr) {
    std::ostringstream out;
    int imm = static_cast<int>(instr.simm16);

    switch (instr.opcode) {
    case SoppOpcode::S_NOP:
        out << "/* S_NOP */";
        break;
    case SoppOpcode::S_ENDPGM:
        out << "return; /* S_ENDPGM */";
        break;
    case SoppOpcode::S_BRANCH:
        out << "/* S_BRANCH offset=" << imm << " */";
        break;
    case SoppOpcode::S_CBRANCH_SCC0:
        out << "if (!scc) { /* branch offset=" << imm << " */ }";
        break;
    case SoppOpcode::S_CBRANCH_SCC1:
        out << "if (scc) { /* branch offset=" << imm << " */ }";
        break;
    case SoppOpcode::S_CBRANCH_VCCZ:
        out << "if (vcc == 0) { /* branch offset=" << imm << " */ }";
        break;
    case SoppOpcode::S_CBRANCH_VCCNZ:
        out << "if (vcc != 0) { /* branch offset=" << imm << " */ }";
        break;
    case SoppOpcode::S_CBRANCH_EXECZ:
        out << "if (exec_mask == 0) { /* branch offset=" << imm << " */ }";
        break;
    case SoppOpcode::S_CBRANCH_EXECNZ:
        out << "if (exec_mask != 0) { /* branch offset=" << imm << " */ }";
        break;
    case SoppOpcode::S_BARRIER:
        out << "GroupMemoryBarrierWithGroupSync(); /* S_BARRIER */";
        break;
    case SoppOpcode::S_WAITCNT:
        out << "/* S_WAITCNT vm=" << (imm & 0xF)
            << " lgkm=" << ((imm >> 4) & 0x3F)
            << " vs=" << ((imm >> 10) & 0xF) << " */";
        break;
    case SoppOpcode::S_WAITCNT_VSCNT:
        out << "/* S_WAITCNT_VSCNT vs=" << (imm & 0xF) << " */";
        break;
    case SoppOpcode::S_WAITCNT_LGKMCNT:
        out << "/* S_WAITCNT_LGKMCNT lgkm=" << (imm & 0x3F) << " */";
        break;
    case SoppOpcode::S_WAITCNT_VMCNT:
        out << "/* S_WAITCNT_VMCNT vm=" << (imm & 0xF) << " */";
        break;
    case SoppOpcode::S_SENDMSG:
        out << "/* S_SENDMSG op=" << (imm & 0xFF) << " */";
        break;
    case SoppOpcode::S_SENDMSGB:
        out << "/* S_SENDMSGB op=" << (imm & 0xFF) << " */";
        break;
    case SoppOpcode::S_TRAP:
        out << "/* S_TRAP id=" << (imm & 0xFF) << " */";
        break;
    case SoppOpcode::S_ICACHE_INV:
        out << "/* S_ICACHE_INV */";
        break;
    case SoppOpcode::S_INCPERFLEVEL:
        out << "/* S_INCPERFLEVEL delta=" << imm << " */";
        break;
    case SoppOpcode::S_DECPERFLEVEL:
        out << "/* S_DECPERFLEVEL delta=" << imm << " */";
        break;
    case SoppOpcode::S_CLAUSE:
        out << "/* S_CLAUSE len=" << (imm & 0x7F) << " */";
        break;
    case SoppOpcode::S_SETVSKIP:
        out << "/* S_SETVSKIP skip=" << (imm & 0x3FFF) << " */";
        break;
    case SoppOpcode::S_SET_GPR_IDX_ON:
        out << "/* S_SET_GPR_IDX_ON */";
        break;
    case SoppOpcode::S_SET_GPR_IDX_OFF:
        out << "/* S_SET_GPR_IDX_OFF */";
        break;
    case SoppOpcode::S_SET_GPR_IDX_MODE:
        out << "/* S_SET_GPR_IDX_MODE mode=" << (imm & 0xF) << " */";
        break;
    case SoppOpcode::S_SETKILL:
        out << "/* S_SETKILL */ discard;";
        break;
    case SoppOpcode::S_SET_SHADER_CYCLES:
        out << "/* S_SET_SHADER_CYCLES */";
        break;
    case SoppOpcode::S_ATOMIC_DEC:
        out << "/* S_ATOMIC_DEC */";
        break;
    case SoppOpcode::S_DCACHE_WB:
        out << "DeviceMemoryBarrier(); /* S_DCACHE_WB */";
        break;
    case SoppOpcode::S_DCACHE_WB_VOL:
        out << "DeviceMemoryBarrier(); /* S_DCACHE_WB_VOL */";
        break;
    case SoppOpcode::S_DCACHE_INV:
        out << "DeviceMemoryBarrier(); /* S_DCACHE_INV */";
        break;
    case SoppOpcode::S_DCACHE_INV_VOL:
        out << "DeviceMemoryBarrier(); /* S_DCACHE_INV_VOL */";
        break;
    case SoppOpcode::S_TTRACEDATA:
        out << "/* S_TTRACEDATA */";
        break;
    case SoppOpcode::S_CBRANCH_CDBGSYS:
        out << "/* S_CBRANCH_CDBGSYS offset=" << imm << " */";
        break;
    case SoppOpcode::S_CBRANCH_CDBGUSER:
        out << "/* S_CBRANCH_CDBGUSER offset=" << imm << " */";
        break;
    case SoppOpcode::S_CBRANCH_CDBGSYS_OR_USER:
        out << "/* S_CBRANCH_CDBGSYS_OR_USER offset=" << imm << " */";
        break;
    default:
        out << "/* UNSUPPORTED SOPP opcode 0x"
            << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
        break;
    }
    return out.str();
}

} // namespace rdna2_to_hlsl
} // namespace dedsec