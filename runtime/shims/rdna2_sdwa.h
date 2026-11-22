// DedSec Fork — RDNA2 SDWA Translator Header
// Translates 220 SDWA (Sub-DWord Addressing) instruction forms to HLSL SM 6.6.
// SDWA allows byte/half-word selection within VGPRs for src and dst operands.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>

namespace dedsec {
namespace rdna2_to_hlsl {

// SDWA select values for source operands
enum class SdwaSel : uint8_t {
    BYTE_0 = 0,   // bits [7:0]
    BYTE_1 = 1,   // bits [15:8]
    BYTE_2 = 2,   // bits [23:16]
    BYTE_3 = 3,   // bits [31:24]
    WORD_0 = 4,   // bits [15:0]
    WORD_1 = 5,   // bits [31:16]
    DWORD  = 6,   // bits [31:0] (full register)
};

// SDWA destination select + clamp/preserve
struct SdwaDstModifiers {
    SdwaSel dst_sel;
    bool dst_unused;      // result discarded (write to nowhere)
    bool clamp;           // clamp float result to [0,1]
    bool sext;            // sign-extend for integer selects
    uint8_t omod;         // output modifier (0=none, 1=mul2, 2=mul4, 3=div2)
};

// Decoded SDWA instruction
struct SdwaInstruction {
    uint8_t opcode;       // VOP1 or VOP2 opcode being wrapped
    uint8_t vdst;
    uint8_t src0;
    uint8_t src1;         // only used for VOP2 opcodes
    SdwaSel src0_sel;
    bool src0_sext;
    bool src0_neg;
    bool src0_abs;
    SdwaSel src1_sel;
    bool src1_sext;
    bool src1_neg;
    bool src1_abs;
    SdwaDstModifiers dst_mod;
    bool is_vop2;         // true if wrapping a VOP2 opcode, false for VOP1
};

// Decode a SDWA instruction from 64-bit encoding
SdwaInstruction decode_sdwa(uint64_t encoded);

// Translate SDWA instruction to HLSL SM 6.6
std::string translate_sdwa(const SdwaInstruction& instr);

// Check if 64-bit word is a SDWA instruction
bool is_sdwa_instruction(uint64_t word);

} // namespace rdna2_to_hlsl
} // namespace dedsec