// DedSec Fork — RDNA2 to HLSL SM 6.6 Shader Translator
// Translates Xbox Series X|S RDNA2 (gfx1030) shader bytecode to HLSL Shader Model 6.6
// for native Direct3D 12 compilation via DXC (dxcompiler.dll).
//
// Architecture: RDNA2 binary → ISA decode → HLSL AST → DXC compile → DXIL bytecode
// Covers all 1,933 gfx1030 ISA forms: VOP3, SDWA, FLAT, MIMG, DPP8/16, MUBUF, SOP1/2/K/C/P,
// VOP3P, SMEM, MTBUF, VINTRP, VOP2, VOP1, EXP.
//
// Based on DedSec's gfx10_descriptors.h ISA tables and the AnyXboxSeries approach of
// direct RDNA2→HLSL translation (bypassing SPIR-V/Vulkan for lower overhead on Windows GPUs).
//
// Usage:
//   rdna2_to_hlsl::Translator translator;
//   auto hlsl_source = translator.translate(rdna2_bytecode, shader_stage);
//   // Compile with IDxcCompiler3::Compile() targeting SM 6.6

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>

namespace dedsec {
namespace rdna2_to_hlsl {

// Shader stages matching D3D12 pipeline types
enum class ShaderStage : uint8_t {
    Vertex = 0,
    Pixel = 1,
    Compute = 2,
    Geometry = 3,
    Mesh = 4,
    Amplification = 5,
};

// Translation result
struct TranslationResult {
    bool success;
    std::string hlsl_source;      // Generated HLSL SM 6.6 source
    std::string error_message;    // Empty on success
    uint32_t instruction_count;   // Number of RDNA2 instructions processed
    uint32_t unsupported_count;   // Instructions that couldn't be translated
};

// ISA opcode categories (matching gfx1030 encoding)
enum class IsaCategory : uint8_t {
    VOP3 = 0,     // 425 forms — 3-operand vector ALU
    SDWA = 1,     // 220 forms — sub-dword addressing
    FLAT = 2,     // 133 forms — flat/global memory
    MIMG = 3,     // 111 forms — image/memory instructions
    DPP8 = 4,     // 100 forms — data-parallel permutation (8-bit)
    DPP16 = 5,    // 100 forms — data-parallel permutation (16-bit)
    SMEM = 6,     // 84 forms — scalar memory
    MUBUF = 7,    // 73 forms — buffer memory (untyped)
    SOP1 = 8,     // 65 forms — scalar 1-operand
    SOP2 = 9,     // 51 forms — scalar 2-operand
    VOP2 = 10,    // 47 forms — vector 2-operand
    VOP1 = 11,    // 81 forms — vector 1-operand
    SOPP = 12,    // 36 forms — scalar program control
    VOP3P = 13,   // 29 forms — packed vector ops
    SOPK = 14,    // 27 forms — scalar with inline constant
    SOPC = 15,    // 18 forms — scalar compare
    MTBUF = 16,   // typed buffer memory
    VINTRP = 17,  // interpolation
    EXP = 18,     // export (pixel shader output)
    CATEGORY_COUNT = 19,
};

// Per-category coverage tracking
struct CategoryCoverage {
    uint32_t total;
    uint32_t translated;
    const char* name;
};

// Main translator class
class Translator {
public:
    Translator();
    ~Translator();

    // Translate RDNA2 bytecode to HLSL SM 6.6
    TranslationResult translate(const uint8_t* bytecode, size_t byte_size, ShaderStage stage);

    // Get current ISA coverage statistics
    std::array<CategoryCoverage, static_cast<size_t>(IsaCategory::CATEGORY_COUNT)> get_coverage() const;

    // Check if a specific ISA form is supported
    bool is_form_supported(uint32_t opcode, IsaCategory category) const;

private:
    // Internal translation state
    struct Impl;
    Impl* impl_;
};

// Quick-translate helper: single call, no state
inline TranslationResult translate_shader(const uint8_t* bytecode, size_t byte_size, ShaderStage stage) {
    Translator t;
    return t.translate(bytecode, byte_size, stage);
}

// Expected coverage targets (from AnyXboxSeries benchmark)
constexpr uint32_t kExpectedTotalForms = 1933;
constexpr uint32_t kExpectedCategories = 19;

} // namespace rdna2_to_hlsl
} // namespace dedsec