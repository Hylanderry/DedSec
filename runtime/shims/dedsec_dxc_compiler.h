// DedSec Fork — DX12 Native Pipeline
// DXC Runtime Compiler — Dynamic loading of dxcompiler.dll for HLSL→DXIL SM 6.6
//
// Architecture:
// - Dynamically loads dxcompiler.dll and dxil.dll at runtime (no link-time dependency)
// - Creates IDxcCompiler3 instance via DxcCreateInstance
// - Compiles HLSL source to DXIL bytecode for any shader stage
// - Caches compiled blobs by source hash to avoid redundant compilation
// - Captures compilation errors/warnings with full diagnostic output
//
// Usage:
//   auto& compiler = dedsec::DxcCompiler::instance();
//   compiler.set_dxc_directory("C:/path/to/dxc");
//   auto result = compiler.compile(hlsl_source, "VSMain", ShaderStage::Vertex);
//   if (result.success) {
//       // result.dxil_blob contains the compiled DXIL bytecode
//       // Use with ID3D12Device::CreateRootSignature or PSO creation
//   }
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace dedsec {

// Shader stage for DXC compilation target
enum class DxcShaderStage : uint8_t {
    Vertex,    // vs_6_6
    Pixel,     // ps_6_6
    Compute,   // cs_6_6
    Geometry,  // gs_6_6
    Hull,      // hs_6_6
    Domain,    // ds_6_6
    Mesh,      // ms_6_6 (SM 6.5+)
    Amplification, // as_6_6 (SM 6.5+)
};

// Compilation result
struct DxcCompileResult {
    bool success;
    std::vector<uint8_t> dxil_blob;      // Compiled DXIL bytecode
    std::vector<uint8_t> pdb_blob;       // Debug PDB (optional, for GPU debugging)
    std::string error_message;            // Compilation errors/warnings
    std::string reflection_data;          // Shader reflection (optional)
    uint64_t compile_time_us;             // Compilation time in microseconds
    uint32_t source_hash;                 // Hash of the HLSL source (for cache lookup)
};

// Compilation options
struct DxcCompileOptions {
    bool debug_info = false;              // Generate PDB debug info
    bool warnings_as_errors = true;       // -WX flag
    bool skip_optimizations = false;      // -Od flag (for debugging)
    uint32_t optimization_level = 3;      // -O0 to -O3
    std::vector<std::string> defines;     // Preprocessor defines (KEY=VALUE)
    std::vector<std::string> include_dirs;// Additional include directories
    std::string entry_point = "main";     // Shader entry point function name
};

// DXC Compiler singleton — thread-safe, lazy-initialized
class DxcCompiler {
public:
    // Get the singleton instance
    static DxcCompiler& instance();

    // Set the directory containing dxcompiler.dll and dxil.dll
    // Must be called before first compile(). Returns false if DLLs not found.
    bool set_dxc_directory(const std::string& path);

    // Set the directory using wide string (for Windows paths)
    bool set_dxc_directory(const wchar_t* path);

    // Check if DXC is available (DLLs loaded successfully)
    bool is_available() const;

    // Get the DXC version string
    std::string get_version() const;

    // Compile HLSL source to DXIL bytecode
    DxcCompileResult compile(
        const std::string& hlsl_source,
        DxcShaderStage stage,
        const DxcCompileOptions& options = {}
    );

    // Compile with custom entry point (convenience overload)
    DxcCompileResult compile(
        const std::string& hlsl_source,
        const std::string& entry_point,
        DxcShaderStage stage,
        const DxcCompileOptions& options = {}
    );

    // Compile from file
    DxcCompileResult compile_file(
        const std::string& hlsl_path,
        DxcShaderStage stage,
        const DxcCompileOptions& options = {}
    );

    // Cache management
    void clear_cache();
    size_t get_cache_size() const;
    uint64_t get_cache_hits() const;
    uint64_t get_cache_misses() const;

    // Shutdown and release all resources (called automatically on destruction)
    void shutdown();

    // Non-copyable
    DxcCompiler(const DxcCompiler&) = delete;
    DxcCompiler& operator=(const DxcCompiler&) = delete;

private:
    DxcCompiler();
    ~DxcCompiler();

    struct Impl;
    Impl* impl_;
};

// Helper: get target profile string for a shader stage
const char* get_dxc_target_profile(DxcShaderStage stage);

// Helper: compute FNV-1a hash of a string
uint32_t hash_string(const std::string& str);

} // namespace dedsec