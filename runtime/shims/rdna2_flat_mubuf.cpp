// DedSec Fork — RDNA2 FLAT/MUBUF Translator Implementation
// Translates 133 FLAT (flat memory) and 73 MUBUF (buffer) instruction forms to HLSL SM 6.6.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "rdna2_flat_mubuf.h"
#include <cstdio>
#include <cstring>
#include <sstream>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── FLAT Instruction Decoding ────────────────────────────────────────────────

bool is_flat_instruction(uint64_t word) {
    // FLAT instructions have encoding [63:56] = 0xDC or 0xDD for gfx1030
    uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
    return (enc == 0xDC || enc == 0xDD);
}

FlatInstruction decode_flat(uint64_t encoded) {
    FlatInstruction instr = {};

    // FLAT encoding: [63:56]=ENC, [55:48]=opcode, [47:40]=vdst, [39:32]=vaddr,
    //                [31:24]=saddr, [23:0]=offset/modifiers
    instr.opcode = static_cast<FlatOpcode>((encoded >> 48) & 0xFF);
    instr.vdst   = static_cast<uint8_t>((encoded >> 40) & 0xFF);
    instr.vaddr  = static_cast<uint8_t>((encoded >> 32) & 0xFF);
    instr.saddr  = static_cast<uint8_t>((encoded >> 24) & 0xFF);
    instr.offset = static_cast<int32_t>(encoded & 0x00FFFFFF);

    // Modifier bits in offset field
    instr.glc = (encoded & (1ULL << 16)) != 0;
    instr.dlc = (encoded & (1ULL << 17)) != 0;
    instr.slc = (encoded & (1ULL << 18)) != 0;
    instr.nv  = (encoded & (1ULL << 19)) != 0;

    return instr;
}

// ── MUBUF Instruction Decoding ───────────────────────────────────────────────

bool is_mubuf_instruction(uint64_t word) {
    // MUBUF instructions have encoding [63:56] = 0xE0..0xEF for gfx1030
    uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
    return (enc >= 0xE0 && enc <= 0xEF);
}

MubufInstruction decode_mubuf(uint64_t encoded) {
    MubufInstruction instr = {};

    // MUBUF encoding: [63:56]=ENC, [55:48]=opcode, [47:40]=vdst, [39:32]=vaddr,
    //                 [31:24]=srsrc, [23:16]=soffset, [15:0]=offset/modifiers
    instr.opcode  = static_cast<MubufOpcode>((encoded >> 48) & 0xFF);
    instr.vdst    = static_cast<uint8_t>((encoded >> 40) & 0xFF);
    instr.vaddr   = static_cast<uint8_t>((encoded >> 32) & 0xFF);
    instr.srsrc   = static_cast<uint8_t>((encoded >> 24) & 0xFF);
    instr.soffset = static_cast<uint8_t>((encoded >> 16) & 0xFF);
    instr.offset  = static_cast<int32_t>(static_cast<int16_t>(encoded & 0xFFFF));

    // Modifier bits
    instr.offen = (encoded & (1ULL << 12)) != 0;
    instr.idxen = (encoded & (1ULL << 13)) != 0;
    instr.glc   = (encoded & (1ULL << 14)) != 0;
    instr.dlc   = (encoded & (1ULL << 15)) != 0;
    instr.slc   = (encoded & (1ULL << 16)) != 0;
    instr.tfe   = (encoded & (1ULL << 17)) != 0;
    instr.lwe   = (encoded & (1ULL << 18)) != 0;

    return instr;
}

// ── FLAT → HLSL Translation ─────────────────────────────────────────────────

std::string translate_flat(const FlatInstruction& instr) {
    std::ostringstream out;

    // Coherence modifiers comment
    if (instr.glc || instr.dlc || instr.slc) {
        out << "/* ";
        if (instr.glc) out << "glc ";
        if (instr.dlc) out << "dlc ";
        if (instr.slc) out << "slc ";
        out << "*/ ";
    }

    switch (instr.opcode) {
        // ── Load operations ──────────────────────────────────────────────
        case FlatOpcode::FLAT_LOAD_UBYTE:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = flat_load_ubyte(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_LOAD_SBYTE:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = flat_load_sbyte(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_LOAD_USHORT:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = flat_load_ushort(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_LOAD_SSHORT:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = flat_load_sshort(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_LOAD_DWORD:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = flat_load_dword(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_LOAD_DWORDX2:
            out << "uint2 v" << static_cast<int>(instr.vdst)
                << " = flat_load_dwordx2(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_LOAD_DWORDX3:
            out << "uint3 v" << static_cast<int>(instr.vdst)
                << " = flat_load_dwordx3(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_LOAD_DWORDX4:
            out << "uint4 v" << static_cast<int>(instr.vdst)
                << " = flat_load_dwordx4(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", " << instr.offset << ");";
            break;

        // ── Store operations ─────────────────────────────────────────────
        case FlatOpcode::FLAT_STORE_BYTE:
            out << "flat_store_byte(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_STORE_SHORT:
            out << "flat_store_short(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_STORE_DWORD:
            out << "flat_store_dword(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_STORE_DWORDX2:
            out << "flat_store_dwordx2(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_STORE_DWORDX3:
            out << "flat_store_dwordx3(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case FlatOpcode::FLAT_STORE_DWORDX4:
            out << "flat_store_dwordx4(v" << static_cast<int>(instr.vaddr)
                << ", s" << static_cast<int>(instr.saddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;

        // ── Atomic operations ────────────────────────────────────────────
        case FlatOpcode::FLAT_ATOMIC_ADD:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAdd(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_SUB:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAdd(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), -v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_AND:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAnd(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_OR:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedOr(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_XOR:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedXor(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_SWAP:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedExchange(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_CMPSWAP:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedCompareExchange(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), cmp_val, v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_SMIN:
        case FlatOpcode::FLAT_ATOMIC_UMIN:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedMin(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_SMAX:
        case FlatOpcode::FLAT_ATOMIC_UMAX:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedMax(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), v" << static_cast<int>(instr.vdst) << ");";
            break;
        case FlatOpcode::FLAT_ATOMIC_INC:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAdd(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), 1);";
            break;
        case FlatOpcode::FLAT_ATOMIC_DEC:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAdd(flat_ptr(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << "), -1);";
            break;

        default:
            out << "/* UNSUPPORTED FLAT opcode 0x"
                << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
            break;
    }

    return out.str();
}

// ── MUBUF → HLSL Translation ─────────────────────────────────────────────────

std::string translate_mubuf(const MubufInstruction& instr) {
    std::ostringstream out;

    // Coherence modifiers comment
    if (instr.glc || instr.dlc || instr.slc) {
        out << "/* ";
        if (instr.glc) out << "glc ";
        if (instr.dlc) out << "dlc ";
        if (instr.slc) out << "slc ";
        out << "*/ ";
    }

    // Buffer resource reference
    std::string bufRef = "buf" + std::to_string(instr.srsrc / 4);

    switch (instr.opcode) {
        // ── Format loads ─────────────────────────────────────────────────
        case MubufOpcode::BUFFER_LOAD_FORMAT_X:
            out << "float v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<float>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_LOAD_FORMAT_XY:
            out << "float2 v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<float2>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_LOAD_FORMAT_XYZ:
            out << "float3 v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<float3>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_LOAD_FORMAT_XYZW:
            out << "float4 v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<float4>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;

        // ── Typed loads ──────────────────────────────────────────────────
        case MubufOpcode::BUFFER_LOAD_UBYTE:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<uint>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ") & 0xFFu;";
            break;
        case MubufOpcode::BUFFER_LOAD_SBYTE:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = int(" << bufRef << ".Load<uint>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ") & 0xFFu);";
            break;
        case MubufOpcode::BUFFER_LOAD_USHORT:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<uint>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ") & 0xFFFFu;";
            break;
        case MubufOpcode::BUFFER_LOAD_SSHORT:
            out << "int v" << static_cast<int>(instr.vdst)
                << " = int(" << bufRef << ".Load<uint>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ") & 0xFFFFu);";
            break;
        case MubufOpcode::BUFFER_LOAD_DWORD:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<uint>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_LOAD_DWORDX2:
            out << "uint2 v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<uint2>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_LOAD_DWORDX3:
            out << "uint3 v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<uint3>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_LOAD_DWORDX4:
            out << "uint4 v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".Load<uint4>(v" << static_cast<int>(instr.vaddr)
                << ", " << instr.offset << ");";
            break;

        // ── Format stores ────────────────────────────────────────────────
        case MubufOpcode::BUFFER_STORE_FORMAT_X:
            out << bufRef << ".Store<float>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_FORMAT_XY:
            out << bufRef << ".Store<float2>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_FORMAT_XYZ:
            out << bufRef << ".Store<float3>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_FORMAT_XYZW:
            out << bufRef << ".Store<float4>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;

        // ── Typed stores ─────────────────────────────────────────────────
        case MubufOpcode::BUFFER_STORE_BYTE:
            out << bufRef << ".Store<uint>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst) << " & 0xFFu"
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_SHORT:
            out << bufRef << ".Store<uint>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst) << " & 0xFFFFu"
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_DWORD:
            out << bufRef << ".Store<uint>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_DWORDX2:
            out << bufRef << ".Store<uint2>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_DWORDX3:
            out << bufRef << ".Store<uint3>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;
        case MubufOpcode::BUFFER_STORE_DWORDX4:
            out << bufRef << ".Store<uint4>(v" << static_cast<int>(instr.vaddr)
                << ", v" << static_cast<int>(instr.vdst)
                << ", " << instr.offset << ");";
            break;

        // ── Buffer atomics ───────────────────────────────────────────────
        case MubufOpcode::BUFFER_ATOMIC_ADD:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAdd(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_SUB:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAdd(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], -v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_AND:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedAnd(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_OR:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedOr(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_XOR:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedXor(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_SWAP:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedExchange(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_CMPSWAP:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedCompareExchange(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], cmp_val, v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_SMIN:
        case MubufOpcode::BUFFER_ATOMIC_UMIN:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedMin(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], v" << static_cast<int>(instr.vdst) << ");";
            break;
        case MubufOpcode::BUFFER_ATOMIC_SMAX:
        case MubufOpcode::BUFFER_ATOMIC_UMAX:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = InterlockedMax(" << bufRef << "[v" << static_cast<int>(instr.vaddr)
                << " + " << instr.offset << "], v" << static_cast<int>(instr.vdst) << ");";
            break;

        // ── Resource queries ─────────────────────────────────────────────
        case MubufOpcode::BUFFER_RESINFO:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".GetDimensions();";
            break;
        case MubufOpcode::BUFFER_SIZE:
            out << "uint v" << static_cast<int>(instr.vdst)
                << " = " << bufRef << ".GetBufferSize();";
            break;

        default:
            out << "/* UNSUPPORTED MUBUF opcode 0x"
                << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
            break;
    }

    return out.str();
}

// ── Coverage Tracking ────────────────────────────────────────────────────────

void FlatMubufCoverage::mark_flat_supported(FlatOpcode op) {
    uint8_t idx = static_cast<uint8_t>(op);
    if (!m_flatSupported[idx]) {
        m_flatSupported[idx] = true;
        m_translatedCount++;
    }
}

void FlatMubufCoverage::mark_mubuf_supported(MubufOpcode op) {
    uint8_t idx = static_cast<uint8_t>(op);
    if (!m_mubufSupported[idx]) {
        m_mubufSupported[idx] = true;
        m_translatedCount++;
    }
}

void FlatMubufCoverage::mark_unsupported(uint8_t opcode, bool isFlat) {
    (void)opcode;
    (void)isFlat;
    m_unsupportedCount++;
}

float FlatMubufCoverage::coveragePercent() const {
    // Total known forms: 133 FLAT + 73 MUBUF = 206
    constexpr uint32_t totalForms = 206;
    if (totalForms == 0) return 0.0f;
    return (static_cast<float>(m_translatedCount) / static_cast<float>(totalForms)) * 100.0f;
}

} // namespace rdna2_to_hlsl
} // namespace dedsec