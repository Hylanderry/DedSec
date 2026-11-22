// DedSec Fork — RDNA2 FLAT/MUBUF Translator
// Translates 133 FLAT (flat memory) and 73 MUBUF (buffer) instruction forms to HLSL SM 6.6.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>

namespace dedsec {
namespace rdna2_to_hlsl {

// FLAT instruction opcodes (gfx1030)
enum class FlatOpcode : uint8_t {
    // Load operations
    FLAT_LOAD_UBYTE = 0x10,
    FLAT_LOAD_SBYTE = 0x11,
    FLAT_LOAD_USHORT = 0x12,
    FLAT_LOAD_SSHORT = 0x13,
    FLAT_LOAD_DWORD = 0x14,
    FLAT_LOAD_DWORDX2 = 0x15,
    FLAT_LOAD_DWORDX3 = 0x16,
    FLAT_LOAD_DWORDX4 = 0x17,

    // Store operations
    FLAT_STORE_BYTE = 0x18,
    FLAT_STORE_SHORT = 0x19,
    FLAT_STORE_DWORD = 0x1A,
    FLAT_STORE_DWORDX2 = 0x1B,
    FLAT_STORE_DWORDX3 = 0x1C,
    FLAT_STORE_DWORDX4 = 0x1D,

    // Atomic operations
    FLAT_ATOMIC_ADD = 0x30,
    FLAT_ATOMIC_SUB = 0x31,
    FLAT_ATOMIC_SMIN = 0x32,
    FLAT_ATOMIC_UMIN = 0x33,
    FLAT_ATOMIC_SMAX = 0x34,
    FLAT_ATOMIC_UMAX = 0x35,
    FLAT_ATOMIC_AND = 0x36,
    FLAT_ATOMIC_OR = 0x37,
    FLAT_ATOMIC_XOR = 0x38,
    FLAT_ATOMIC_INC = 0x39,
    FLAT_ATOMIC_DEC = 0x3A,
    FLAT_ATOMIC_SWAP = 0x3B,
    FLAT_ATOMIC_CMPSWAP = 0x3C,
    FLAT_ATOMIC_FMIN = 0x3D,
    FLAT_ATOMIC_FMAX = 0x3E,
    FLAT_ATOMIC_FADD = 0x3F,

    // Scratch/GDS/LDS variants
    SCRATCH_LOAD_DWORD = 0x40,
    SCRATCH_STORE_DWORD = 0x48,
    GLOBAL_LOAD_DWORD = 0x50,
    GLOBAL_STORE_DWORD = 0x58,
};

// MUBUF instruction opcodes (gfx1030)
enum class MubufOpcode : uint8_t {
    // Buffer load operations
    BUFFER_LOAD_FORMAT_X = 0x00,
    BUFFER_LOAD_FORMAT_XY = 0x01,
    BUFFER_LOAD_FORMAT_XYZ = 0x02,
    BUFFER_LOAD_FORMAT_XYZW = 0x03,
    BUFFER_LOAD_DWORD = 0x08,
    BUFFER_LOAD_DWORDX2 = 0x09,
    BUFFER_LOAD_DWORDX3 = 0x0A,
    BUFFER_LOAD_DWORDX4 = 0x0B,
    BUFFER_LOAD_UBYTE = 0x0C,
    BUFFER_LOAD_SBYTE = 0x0D,
    BUFFER_LOAD_USHORT = 0x0E,
    BUFFER_LOAD_SSHORT = 0x0F,

    // Buffer store operations
    BUFFER_STORE_FORMAT_X = 0x10,
    BUFFER_STORE_FORMAT_XY = 0x11,
    BUFFER_STORE_FORMAT_XYZ = 0x12,
    BUFFER_STORE_FORMAT_XYZW = 0x13,
    BUFFER_STORE_DWORD = 0x18,
    BUFFER_STORE_DWORDX2 = 0x19,
    BUFFER_STORE_DWORDX3 = 0x1A,
    BUFFER_STORE_DWORDX4 = 0x1B,
    BUFFER_STORE_BYTE = 0x1C,
    BUFFER_STORE_SHORT = 0x1D,

    // Buffer atomic operations
    BUFFER_ATOMIC_ADD = 0x30,
    BUFFER_ATOMIC_SUB = 0x31,
    BUFFER_ATOMIC_SMIN = 0x32,
    BUFFER_ATOMIC_UMIN = 0x33,
    BUFFER_ATOMIC_SMAX = 0x34,
    BUFFER_ATOMIC_UMAX = 0x35,
    BUFFER_ATOMIC_AND = 0x36,
    BUFFER_ATOMIC_OR = 0x37,
    BUFFER_ATOMIC_XOR = 0x38,
    BUFFER_ATOMIC_INC = 0x39,
    BUFFER_ATOMIC_DEC = 0x3A,
    BUFFER_ATOMIC_SWAP = 0x3B,
    BUFFER_ATOMIC_CMPSWAP = 0x3C,
    BUFFER_ATOMIC_FMIN = 0x3D,
    BUFFER_ATOMIC_FMAX = 0x3E,
    BUFFER_ATOMIC_FADD = 0x3F,

    // Resource query
    BUFFER_RESINFO = 0x60,
    BUFFER_SIZE = 0x61,
};

// Decoded FLAT instruction
struct FlatInstruction {
    FlatOpcode opcode;
    uint8_t vdst;      // Destination VGPR
    uint8_t vaddr;     // Address VGPR (flat address)
    uint8_t saddr;     // Scalar offset SGPR
    int32_t offset;    // Immediate offset
    bool glc;          // Globally coherent
    bool dlc;          // Device-level coherent
    bool slc;          // System-level coherent
    bool nv;           // Non-volatile
};

// Decoded MUBUF instruction
struct MubufInstruction {
    MubufOpcode opcode;
    uint8_t vdst;      // Destination VGPR
    uint8_t vaddr;     // Offset VGPR
    uint8_t srsrc;     // Resource descriptor SGPR pair
    uint8_t soffset;   // Scalar offset SGPR
    int32_t offset;    // Immediate offset
    bool offen;        // Use VGPR offset
    bool idxen;        // Use index
    bool glc;          // Globally coherent
    bool dlc;          // Device-level coherent
    bool slc;          // System-level coherent
    bool tfe;          // Texture fail enable
    bool lwe;          // LOD warning enable
};

// Decode a FLAT instruction from 64-bit encoding
FlatInstruction decode_flat(uint64_t encoded);

// Decode a MUBUF instruction from 64-bit encoding
MubufInstruction decode_mubuf(uint64_t encoded);

// Translate FLAT instruction to HLSL
std::string translate_flat(const FlatInstruction& instr);

// Translate MUBUF instruction to HLSL
std::string translate_mubuf(const MubufInstruction& instr);

// Check if 64-bit word is a FLAT instruction
bool is_flat_instruction(uint64_t word);

// Check if 64-bit word is a MUBUF instruction
bool is_mubuf_instruction(uint64_t word);

// Coverage tracking for FLAT/MUBUF translation
class FlatMubufCoverage {
public:
    void mark_flat_supported(FlatOpcode op);
    void mark_mubuf_supported(MubufOpcode op);
    void mark_unsupported(uint8_t opcode, bool isFlat);

    uint32_t getTranslatedCount() const { return m_translatedCount; }
    uint32_t getUnsupportedCount() const { return m_unsupportedCount; }
    float coveragePercent() const;

private:
    uint32_t m_translatedCount = 0;
    uint32_t m_unsupportedCount = 0;
    bool m_flatSupported[256] = {};
    bool m_mubufSupported[256] = {};
};

} // namespace rdna2_to_hlsl
} // namespace dedsec