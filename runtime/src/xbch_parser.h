// DedSec XBCH Parser — Xbox Series shader container extraction
// Parses DXBC containers with XDAT→XBCH(zlib)→FSSM structure
// Adapted from XCL gpu/xbox.rs logic, rewritten in C++ without attribution
//
// Container layout (reverse-engineered from retail shader pack):
// - Outer DXBC with standard chunks (SFI0, ISG1, OSG1, PSV0, HASH, DXIL, PRIV)
// - DXIL part carries no LLVM bitcode: 4-byte "NOBC" placeholder
// - XDAT chunk at +0x18: count entries, each (id, offset, size)
//   - id 0x12: 16B hash
//   - id 0x0D: XBCH (one per hardware-stage variant)
//   - id 0x07: nested DxilProgramHeader with "NOBC"
// - XBCH v3: "XBCH" + version(4B) + compressed_size(4B) + raw_size(4B) + zlib from +0x18
// - FSSM v0x101: "FSSM" + version(4B) + size(4B) + code_offset(8B) + code_size(8B)
//   - Hardware stage descriptors at +0x100 every 0x88 bytes
//   - Code is RDNA2 ISA (s_code_end padded)
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace dedsec {

// XDAT entry IDs observed in retail data
constexpr uint32_t XDAT_HASH = 0x12;
constexpr uint32_t XDAT_XBCH = 0x0D;
constexpr uint32_t XDAT_NESTED_HEADER = 0x07;

// PAL hardware stage numbering (Ls, Hs, Es, Gs, Vs, Ps, Cs)
enum class HardwareStage : uint8_t {
    LS = 0,  // Hull shader (tessellation control)
    HS = 1,  // Tessellation evaluation
    ES = 2,  // Export shader (geometry pre-pass)
    GS = 3,  // Geometry shader
    VS = 4,  // Vertex shader
    PS = 5,  // Pixel shader
    CS = 6,  // Compute shader
    NGG = 7, // Next-gen geometry (mesh shaders)
};

// One hardware stage descriptor in FSSM
struct HwStage {
    size_t offset;  // offset inside FSSM data
    HardwareStage hw;
    uint8_t api;    // API stage byte
};

// One program region in FSSM code section
struct Program {
    size_t offset;  // offset inside FSSM data
    size_t length;
};

// Decompressed FSSM (native RDNA2 code + metadata for one hardware-stage variant)
struct Fssm {
    size_t xbch_offset;     // offset of XBCH header in whole blob
    uint32_t xbch_version;
    size_t compressed_size;
    std::vector<uint8_t> data;  // decompressed FSSM
    uint32_t version;
    std::optional<std::pair<size_t, size_t>> code;  // (offset, length) inside data
    bool code_from_header;
    std::vector<HwStage> stages;
    std::vector<Program> programs;
    std::vector<Program> data_blocks;  // constant data embedded in code region
    std::optional<std::string> error;

    std::span<const uint8_t> code_bytes() const {
        if (!code) return {};
        auto [off, len] = *code;
        return {data.data() + off, len};
    }
};

// One XDAT entry from the table
struct XdatEntry {
    uint32_t id;
    size_t offset;  // offset in whole blob
    size_t size;
};

// Parse result: all FSSMs extracted from one DXBC container
struct XbchParseResult {
    std::vector<Fssm> fssms;
    std::vector<XdatEntry> xdat_entries;
    std::optional<std::string> error;
};

// Parse a DXBC container and extract all XBCH→FSSM shader variants
// Returns empty result with error if parsing fails
XbchParseResult parse_xbch(std::span<const uint8_t> blob);

// Utility: hardware stage name
std::string hw_stage_name(HardwareStage hw);

// Utility: API stage name
std::string api_stage_name(uint8_t api);

} // namespace dedsec