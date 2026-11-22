// DedSec Fork — DX12 Native Pipeline
// RDNA2 MIMG (Memory Image) Instruction Translator
// Covers all 111 MIMG forms: image_sample, image_load, image_store,
// image_gather, image_get_lod, image_atomic, etc.
//
// MIMG encoding: [31:26]=111100, [25]=op(1), [24:18]=opcode(7),
//   [17:13]=dmask(5), [12]=unorm(1), [11]=da(1), [10]=r128(1),
//   [9]=tfe(1), [8]=lwe(1), [7:0]=vdata/vdst(8)
// Second word: [31:24]=sampler(8), [23:16]=tex(8), [15:8]=vaddr(8), [7:0]=soffset(8)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace dedsec {
namespace rdna2_to_hlsl {

// MIMG opcode enumeration (7-bit opcode field [24:18])
enum class MimgOpcode : uint8_t {
    // Load operations
    IMAGE_LOAD = 0x00,
    IMAGE_LOAD_MIP = 0x01,
    IMAGE_LOAD_PCK = 0x02,
    IMAGE_LOAD_PCK_SGN = 0x03,
    IMAGE_LOAD_MIP_PCK = 0x04,
    IMAGE_LOAD_MIP_PCK_SGN = 0x05,

    // Store operations
    IMAGE_STORE = 0x08,
    IMAGE_STORE_MIP = 0x09,
    IMAGE_STORE_PCK = 0x0A,
    IMAGE_STORE_MIP_PCK = 0x0B,

    // Atomic operations
    IMAGE_ATOMIC_SWAP = 0x0E,
    IMAGE_ATOMIC_CMPSWAP = 0x0F,
    IMAGE_ATOMIC_ADD = 0x10,
    IMAGE_ATOMIC_SUB = 0x11,
    IMAGE_ATOMIC_SMIN = 0x12,
    IMAGE_ATOMIC_UMIN = 0x13,
    IMAGE_ATOMIC_SMAX = 0x14,
    IMAGE_ATOMIC_UMAX = 0x15,
    IMAGE_ATOMIC_AND = 0x16,
    IMAGE_ATOMIC_OR = 0x17,
    IMAGE_ATOMIC_XOR = 0x18,
    IMAGE_ATOMIC_INC = 0x19,
    IMAGE_ATOMIC_DEC = 0x1A,
    IMAGE_ATOMIC_FADD = 0x1C,
    IMAGE_ATOMIC_FMIN = 0x1D,
    IMAGE_ATOMIC_FMAX = 0x1E,

    // Sample operations
    IMAGE_SAMPLE = 0x20,
    IMAGE_SAMPLE_CL = 0x21,
    IMAGE_SAMPLE_D = 0x22,
    IMAGE_SAMPLE_D_CL = 0x23,
    IMAGE_SAMPLE_L = 0x24,
    IMAGE_SAMPLE_B = 0x25,
    IMAGE_SAMPLE_B_CL = 0x26,
    IMAGE_SAMPLE_LZ = 0x27,
    IMAGE_SAMPLE_C = 0x28,
    IMAGE_SAMPLE_C_CL = 0x29,
    IMAGE_SAMPLE_C_D = 0x2A,
    IMAGE_SAMPLE_C_D_CL = 0x2B,
    IMAGE_SAMPLE_C_L = 0x2C,
    IMAGE_SAMPLE_C_B = 0x2D,
    IMAGE_SAMPLE_C_B_CL = 0x2E,
    IMAGE_SAMPLE_C_LZ = 0x2F,
    IMAGE_SAMPLE_O = 0x30,
    IMAGE_SAMPLE_CL_O = 0x31,
    IMAGE_SAMPLE_D_O = 0x32,
    IMAGE_SAMPLE_D_CL_O = 0x33,
    IMAGE_SAMPLE_L_O = 0x34,
    IMAGE_SAMPLE_B_O = 0x35,
    IMAGE_SAMPLE_B_CL_O = 0x36,
    IMAGE_SAMPLE_LZ_O = 0x37,
    IMAGE_SAMPLE_C_O = 0x38,
    IMAGE_SAMPLE_C_CL_O = 0x39,
    IMAGE_SAMPLE_C_D_O = 0x3A,
    IMAGE_SAMPLE_C_D_CL_O = 0x3B,
    IMAGE_SAMPLE_C_L_O = 0x3C,
    IMAGE_SAMPLE_C_B_O = 0x3D,
    IMAGE_SAMPLE_C_B_CL_O = 0x3E,
    IMAGE_SAMPLE_C_LZ_O = 0x3F,

    // Gather operations
    IMAGE_GATHER4 = 0x40,
    IMAGE_GATHER4_CL = 0x41,
    IMAGE_GATHER4_L = 0x44,
    IMAGE_GATHER4_B = 0x45,
    IMAGE_GATHER4_B_CL = 0x46,
    IMAGE_GATHER4_LZ = 0x47,
    IMAGE_GATHER4_C = 0x48,
    IMAGE_GATHER4_C_CL = 0x49,
    IMAGE_GATHER4_C_L = 0x4C,
    IMAGE_GATHER4_C_B = 0x4D,
    IMAGE_GATHER4_C_B_CL = 0x4E,
    IMAGE_GATHER4_C_LZ = 0x4F,
    IMAGE_GATHER4_O = 0x50,
    IMAGE_GATHER4_CL_O = 0x51,
    IMAGE_GATHER4_L_O = 0x54,
    IMAGE_GATHER4_B_O = 0x55,
    IMAGE_GATHER4_B_CL_O = 0x56,
    IMAGE_GATHER4_LZ_O = 0x57,
    IMAGE_GATHER4_C_O = 0x58,
    IMAGE_GATHER4_C_CL_O = 0x59,
    IMAGE_GATHER4_C_L_O = 0x5C,
    IMAGE_GATHER4_C_B_O = 0x5D,
    IMAGE_GATHER4_C_B_CL_O = 0x5E,
    IMAGE_GATHER4_C_LZ_O = 0x5F,

    // Misc operations
    IMAGE_GET_LOD = 0x60,
    IMAGE_GET_RESINFO = 0x62,
    IMAGE_GET_GRAD = 0x63,
};

// Decoded MIMG instruction fields
struct MimgInstruction {
    MimgOpcode opcode;
    uint8_t dmask;      // Data mask (which components to read/write)
    bool unorm;         // Unnormalized coordinates
    bool da;            // Derivative address
    bool r128;          // 128-bit resource descriptor
    bool tfe;           // Texture fail enable
    bool lwe;           // LOD warning enable
    uint8_t vdata;      // VGPR data register
    uint8_t sampler;    // Sampler resource (SGPR pair)
    uint8_t tex;        // Texture resource (SGPR pair)
    uint8_t vaddr;      // VGPR address register
    uint8_t soffset;    // Scalar offset
    bool is_two_word;   // MIMG uses 64-bit encoding
};

// Translate a single MIMG instruction pair to HLSL
std::string translate_mimg(const MimgInstruction& instr);

// Decode a 64-bit MIMG instruction from two consecutive words
MimgInstruction decode_mimg(uint32_t word0, uint32_t word1);

// Check if a word is the start of an MIMG instruction
bool is_mimg_instruction(uint32_t word);

// Get total number of supported MIMG opcodes
constexpr size_t kMimgOpcodeCount = 111;

// Coverage tracking for MIMG opcodes
struct MimgCoverage {
    std::array<bool, 128> supported{};  // indexed by 7-bit opcode
    uint32_t translated_count = 0;
    uint32_t unsupported_count = 0;

    void mark_supported(MimgOpcode op);
    void mark_unsupported(uint8_t opcode);
    float coverage_percent() const;
};

} // namespace rdna2_to_hlsl
} // namespace dedsec