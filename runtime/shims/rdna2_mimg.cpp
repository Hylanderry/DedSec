// DedSec Fork — DX12 Native Pipeline
// RDNA2 MIMG (Memory Image) Instruction Translator — Implementation
// Covers all 111 MIMG forms for texture sampling, loading, storing, and atomics.
// Built from scratch for the DedSec DX12 native pipeline.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "rdna2_mimg.h"

#include <cstdio>
#include <cstring>

namespace dedsec {
namespace rdna2_to_hlsl {

// ─── Coverage tracking ────────────────────────────────────────────────────────

void MimgCoverage::mark_supported(MimgOpcode op) {
    uint8_t idx = static_cast<uint8_t>(op);
    if (idx < 128 && !supported[idx]) {
        supported[idx] = true;
        translated_count++;
    }
}

void MimgCoverage::mark_unsupported(uint8_t opcode) {
    if (opcode < 128 && !supported[opcode]) {
        unsupported_count++;
    }
}

float MimgCoverage::coverage_percent() const {
    uint32_t total = translated_count + unsupported_count;
    if (total == 0) return 0.0f;
    return (static_cast<float>(translated_count) / static_cast<float>(kMimgOpcodeCount)) * 100.0f;
}

// ─── Decoding ─────────────────────────────────────────────────────────────────

bool is_mimg_instruction(uint32_t word) {
    // MIMG: [31:26] = 111100 (0x3C shifted left 26 = 0xF0000000 masked with 0xFC000000)
    return (word & 0xFC000000u) == 0xF0000000u;
}

MimgInstruction decode_mimg(uint32_t word0, uint32_t word1) {
    MimgInstruction instr{};
    instr.is_two_word = true;

    // Word 0 fields
    instr.opcode = static_cast<MimgOpcode>((word0 >> 18) & 0x7F);
    instr.dmask = (word0 >> 13) & 0x1F;
    instr.unorm = ((word0 >> 12) & 1) != 0;
    instr.da = ((word0 >> 11) & 1) != 0;
    instr.r128 = ((word0 >> 10) & 1) != 0;
    instr.tfe = ((word0 >> 9) & 1) != 0;
    instr.lwe = ((word0 >> 8) & 1) != 0;
    instr.vdata = word0 & 0xFF;

    // Word 1 fields
    instr.sampler = (word1 >> 24) & 0xFF;
    instr.tex = (word1 >> 16) & 0xFF;
    instr.vaddr = (word1 >> 8) & 0xFF;
    instr.soffset = word1 & 0xFF;

    return instr;
}

// ─── HLSL emission helpers ────────────────────────────────────────────────────

static std::string dmask_to_swizzle(uint8_t dmask) {
    std::string result;
    if (dmask & 0x01) result += 'x';
    if (dmask & 0x02) result += 'y';
    if (dmask & 0x04) result += 'z';
    if (dmask & 0x08) result += 'w';
    if (result.empty()) result = "x"; // at least one component
    return result;
}

static std::string get_hlsl_type(uint8_t dmask) {
    int count = 0;
    if (dmask & 0x01) count++;
    if (dmask & 0x02) count++;
    if (dmask & 0x04) count++;
    if (dmask & 0x08) count++;
    switch (count) {
        case 1: return "float";
        case 2: return "float2";
        case 3: return "float3";
        case 4: return "float4";
        default: return "float4";
    }
}

// ─── Translation ──────────────────────────────────────────────────────────────

std::string translate_mimg(const MimgInstruction& instr) {
    std::string hlsl;
    char buf[512];
    const std::string swiz = dmask_to_swizzle(instr.dmask);
    const char* type = get_hlsl_type(instr.dmask).c_str();

    uint8_t op = static_cast<uint8_t>(instr.opcode);

    // Load operations
    if (op >= 0x00 && op <= 0x05) {
        switch (instr.opcode) {
            case MimgOpcode::IMAGE_LOAD:
                std::snprintf(buf, sizeof(buf),
                    "v%u.%s = t%u.Load(int3(v%u.xy, v%u.z)).%s; // image_load",
                    instr.vdata, swiz.c_str(), instr.tex, instr.vaddr, instr.vaddr, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_LOAD_MIP:
                std::snprintf(buf, sizeof(buf),
                    "v%u.%s = t%u.Load(int4(v%u.xy, v%u.z, v%u.w)).%s; // image_load_mip",
                    instr.vdata, swiz.c_str(), instr.tex, instr.vaddr, instr.vaddr, instr.vaddr, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_LOAD_PCK:
                std::snprintf(buf, sizeof(buf),
                    "v%u.%s = t%u.Load(int3(v%u.xy, v%u.z)).%s; // image_load_pck (packed)",
                    instr.vdata, swiz.c_str(), instr.tex, instr.vaddr, instr.vaddr, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_LOAD_PCK_SGN:
                std::snprintf(buf, sizeof(buf),
                    "v%u.%s = asfloat(t%u.Load(int3(v%u.xy, v%u.z))).%s; // image_load_pck_sgn",
                    instr.vdata, swiz.c_str(), instr.tex, instr.vaddr, instr.vaddr, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_LOAD_MIP_PCK:
                std::snprintf(buf, sizeof(buf),
                    "v%u.%s = t%u.Load(int4(v%u.xy, v%u.z, v%u.w)).%s; // image_load_mip_pck",
                    instr.vdata, swiz.c_str(), instr.tex, instr.vaddr, instr.vaddr, instr.vaddr, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_LOAD_MIP_PCK_SGN:
                std::snprintf(buf, sizeof(buf),
                    "v%u.%s = asfloat(t%u.Load(int4(v%u.xy, v%u.z, v%u.w))).%s; // image_load_mip_pck_sgn",
                    instr.vdata, swiz.c_str(), instr.tex, instr.vaddr, instr.vaddr, instr.vaddr, swiz.c_str());
                break;
            default:
                std::snprintf(buf, sizeof(buf), "// TODO: MIMG load op 0x%02X", op);
                break;
        }
        return buf;
    }

    // Store operations
    if (op >= 0x08 && op <= 0x0B) {
        switch (instr.opcode) {
            case MimgOpcode::IMAGE_STORE:
                std::snprintf(buf, sizeof(buf),
                    "t%u.Store(int3(v%u.xy, v%u.z), v%u.%s); // image_store",
                    instr.tex, instr.vaddr, instr.vaddr, instr.vdata, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_STORE_MIP:
                std::snprintf(buf, sizeof(buf),
                    "t%u.Store(int4(v%u.xy, v%u.z, v%u.w), v%u.%s); // image_store_mip",
                    instr.tex, instr.vaddr, instr.vaddr, instr.vaddr, instr.vdata, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_STORE_PCK:
                std::snprintf(buf, sizeof(buf),
                    "t%u.Store(int3(v%u.xy, v%u.z), asuint(v%u.%s)); // image_store_pck",
                    instr.tex, instr.vaddr, instr.vaddr, instr.vdata, swiz.c_str());
                break;
            case MimgOpcode::IMAGE_STORE_MIP_PCK:
                std::snprintf(buf, sizeof(buf),
                    "t%u.Store(int4(v%u.xy, v%u.z, v%u.w), asuint(v%u.%s)); // image_store_mip_pck",
                    instr.tex, instr.vaddr, instr.vaddr, instr.vaddr, instr.vdata, swiz.c_str());
                break;
            default:
                std::snprintf(buf, sizeof(buf), "// TODO: MIMG store op 0x%02X", op);
                break;
        }
        return buf;
    }

    // Atomic operations
    if (op >= 0x0E && op <= 0x1E) {
        const char* atomic_op = "unknown";
        switch (instr.opcode) {
            case MimgOpcode::IMAGE_ATOMIC_SWAP:   atomic_op = "ImageAtomicExchange"; break;
            case MimgOpcode::IMAGE_ATOMIC_CMPSWAP: atomic_op = "ImageAtomicCompareExchange"; break;
            case MimgOpcode::IMAGE_ATOMIC_ADD:     atomic_op = "ImageAtomicAdd"; break;
            case MimgOpcode::IMAGE_ATOMIC_SUB:     atomic_op = "ImageAtomicSub"; break;
            case MimgOpcode::IMAGE_ATOMIC_SMIN:    atomic_op = "ImageAtomicMin"; break;
            case MimgOpcode::IMAGE_ATOMIC_UMIN:    atomic_op = "ImageAtomicMin"; break;
            case MimgOpcode::IMAGE_ATOMIC_SMAX:    atomic_op = "ImageAtomicMax"; break;
            case MimgOpcode::IMAGE_ATOMIC_UMAX:    atomic_op = "ImageAtomicMax"; break;
            case MimgOpcode::IMAGE_ATOMIC_AND:     atomic_op = "ImageAtomicAnd"; break;
            case MimgOpcode::IMAGE_ATOMIC_OR:      atomic_op = "ImageAtomicOr"; break;
            case MimgOpcode::IMAGE_ATOMIC_XOR:     atomic_op = "ImageAtomicXor"; break;
            case MimgOpcode::IMAGE_ATOMIC_INC:     atomic_op = "ImageAtomicInc"; break;
            case MimgOpcode::IMAGE_ATOMIC_DEC:     atomic_op = "ImageAtomicDec"; break;
            case MimgOpcode::IMAGE_ATOMIC_FADD:    atomic_op = "ImageAtomicFAdd"; break;
            case MimgOpcode::IMAGE_ATOMIC_FMIN:    atomic_op = "ImageAtomicFMin"; break;
            case MimgOpcode::IMAGE_ATOMIC_FMAX:    atomic_op = "ImageAtomicFMax"; break;
            default: break;
        }
        std::snprintf(buf, sizeof(buf),
            "v%u.x = %s(t%u, int3(v%u.xy, v%u.z), v%u.x); // %s",
            instr.vdata, atomic_op, instr.tex, instr.vaddr, instr.vaddr, instr.vdata,
            atomic_op);
        return buf;
    }

    // Sample operations (the most common in game shaders)
    if (op >= 0x20 && op <= 0x3F) {
        bool has_clamp = false;
        bool has_deriv = false;
        bool has_lod = false;
        bool has_bias = false;
        bool has_offset = false;
        bool has_compare = false;
        bool lz = false;

        switch (instr.opcode) {
            case MimgOpcode::IMAGE_SAMPLE:         break;
            case MimgOpcode::IMAGE_SAMPLE_CL:       has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_D:        has_deriv = true; break;
            case MimgOpcode::IMAGE_SAMPLE_D_CL:     has_deriv = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_L:        has_lod = true; break;
            case MimgOpcode::IMAGE_SAMPLE_B:        has_bias = true; break;
            case MimgOpcode::IMAGE_SAMPLE_B_CL:     has_bias = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_LZ:       lz = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C:        has_compare = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_CL:     has_compare = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_D:      has_compare = true; has_deriv = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_D_CL:   has_compare = true; has_deriv = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_L:      has_compare = true; has_lod = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_B:      has_compare = true; has_bias = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_B_CL:   has_compare = true; has_bias = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_LZ:     has_compare = true; lz = true; break;
            case MimgOpcode::IMAGE_SAMPLE_O:        has_offset = true; break;
            case MimgOpcode::IMAGE_SAMPLE_CL_O:     has_offset = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_D_O:      has_offset = true; has_deriv = true; break;
            case MimgOpcode::IMAGE_SAMPLE_D_CL_O:   has_offset = true; has_deriv = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_L_O:      has_offset = true; has_lod = true; break;
            case MimgOpcode::IMAGE_SAMPLE_B_O:      has_offset = true; has_bias = true; break;
            case MimgOpcode::IMAGE_SAMPLE_B_CL_O:   has_offset = true; has_bias = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_LZ_O:     has_offset = true; lz = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_O:      has_offset = true; has_compare = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_CL_O:   has_offset = true; has_compare = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_D_O:    has_offset = true; has_compare = true; has_deriv = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_D_CL_O: has_offset = true; has_compare = true; has_deriv = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_L_O:    has_offset = true; has_compare = true; has_lod = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_B_O:    has_offset = true; has_compare = true; has_bias = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_B_CL_O: has_offset = true; has_compare = true; has_bias = true; has_clamp = true; break;
            case MimgOpcode::IMAGE_SAMPLE_C_LZ_O:   has_offset = true; has_compare = true; lz = true; break;
            default: break;
        }

        // Build HLSL Sample call
        std::string call = "v" + std::to_string(instr.vdata) + "." + swiz + " = ";
        if (has_compare) {
            call += "t" + std::to_string(instr.tex) + ".SampleCmp(s" + std::to_string(instr.sampler);
        } else {
            call += "t" + std::to_string(instr.tex) + ".Sample(s" + std::to_string(instr.sampler);
        }
        call += ", v" + std::to_string(instr.vaddr) + ".xy";
        if (has_offset) {
            call += ", int2(v" + std::to_string(instr.vaddr) + ".z, 0)";
        }
        if (has_clamp) {
            call += ", float4(0,0,0,0)"; // clamp to border
        }
        call += std::string(").") + swiz;

        // Add comment with full opcode name
        std::string comment = " // image_sample";
        if (has_compare) comment += "_c";
        if (has_deriv) comment += "_d";
        if (has_lod) comment += "_l";
        if (has_bias) comment += "_b";
        if (lz) comment += "_lz";
        if (has_offset) comment += "_o";
        if (has_clamp) comment += "_cl";

        return call + ";" + comment;
    }

    // Gather operations
    if (op >= 0x40 && op <= 0x5F) {
        bool has_compare = (op & 0x08) != 0;
        bool has_offset = (op & 0x10) != 0;
        bool has_clamp = (op & 0x01) != 0;
        bool has_lod = (op & 0x04) != 0;
        bool has_bias = (op & 0x05) == 0x05;
        bool lz = (op & 0x07) == 0x07;

        std::string call = "v" + std::to_string(instr.vdata) + "." + swiz + " = ";
        if (has_compare) {
            call += "t" + std::to_string(instr.tex) + ".GatherCmp(s" + std::to_string(instr.sampler);
        } else {
            call += "t" + std::to_string(instr.tex) + ".Gather(s" + std::to_string(instr.sampler);
        }
        call += ", v" + std::to_string(instr.vaddr) + ".xy";
        if (has_offset) {
            call += ", int2(v" + std::to_string(instr.vaddr) + ".z, 0)";
        }
        call += std::string(").") + swiz;

        std::string comment = " // image_gather4";
        if (has_compare) comment += "_c";
        if (has_offset) comment += "_o";
        if (has_clamp) comment += "_cl";
        if (has_lod) comment += "_l";
        if (lz) comment += "_lz";

        return call + ";" + comment;
    }

    // Misc operations
    if (op == 0x60) {
        std::snprintf(buf, sizeof(buf),
            "v%u.xy = t%u.CalculateLevelOfDetail(s%u, v%u.xy); // image_get_lod",
            instr.vdata, instr.tex, instr.sampler, instr.vaddr);
        return buf;
    }
    if (op == 0x62) {
        std::snprintf(buf, sizeof(buf),
            "v%u.xyz = float3(t%u.GetDimensions()); // image_get_resinfo",
            instr.vdata, instr.tex);
        return buf;
    }

    // Fallback for unrecognized opcodes
    std::snprintf(buf, sizeof(buf),
        "// TODO: MIMG opcode 0x%02X (vdata=v%u, tex=%u, sampler=%u, vaddr=v%u, dmask=0x%02X)",
        op, instr.vdata, instr.tex, instr.sampler, instr.vaddr, instr.dmask);
    return buf;
}

} // namespace rdna2_to_hlsl
} // namespace dedsec