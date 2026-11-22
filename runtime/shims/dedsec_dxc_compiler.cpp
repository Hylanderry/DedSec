// DedSec Fork — DX12 Native Pipeline
// DXC Runtime Compiler — Implementation
// Built from scratch for the DedSec DX12 native pipeline.
//
// Dynamically loads dxcompiler.dll and dxil.dll at runtime, creates an
// IDxcCompiler3 instance, and compiles HLSL SM 6.6 source to DXIL bytecode.
// Includes a thread-safe source-hash cache to avoid redundant compilation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#define DEDSEC_DXC_COMPILER_IMPLEMENTATION
#include "dedsec_dxc_compiler.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxcapi.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace dedsec {

// ─── Helpers ──────────────────────────────────────────────────────────────────

const char* get_dxc_target_profile(DxcShaderStage stage) {
    switch (stage) {
        case DxcShaderStage::Vertex:        return "vs_6_6";
        case DxcShaderStage::Pixel:         return "ps_6_6";
        case DxcShaderStage::Compute:       return "cs_6_6";
        case DxcShaderStage::Geometry:      return "gs_6_6";
        case DxcShaderStage::Hull:          return "hs_6_6";
        case DxcShaderStage::Domain:        return "ds_6_6";
        case DxcShaderStage::Mesh:          return "ms_6_6";
        case DxcShaderStage::Amplification: return "as_6_6";
        default:                            return "cs_6_6";
    }
}

uint32_t hash_string(const std::string& str) {
    uint32_t hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        hash *= 16777619u;
    }
    return hash;
}

// ─── Internal state ──────────────────────────────────────────────────────────

struct DxcCompiler::Impl {
    HMODULE h_dxc = nullptr;
    HMODULE h_dxil = nullptr;
    ComPtr<IDxcCompiler3> compiler;
    bool available = false;
    std::wstring dxc_directory;
    std::string version_string;

    struct CacheEntry {
        std::vector<uint8_t> dxil;
        std::string errors;
        uint64_t compile_time_us;
    };
    std::unordered_map<uint32_t, CacheEntry> cache;
    mutable std::mutex cache_mutex;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;

    ~Impl() { shutdown(); }

    void shutdown() {
        compiler.Reset();
        if (h_dxc)  { FreeLibrary(h_dxc);  h_dxc  = nullptr; }
        if (h_dxil) { FreeLibrary(h_dxil); h_dxil = nullptr; }
        available = false;
        cache.clear();
        cache_hits = 0;
        cache_misses = 0;
    }

    bool load_dlls(const wchar_t* dir) {
        if (available) return true;

        std::wstring dxc_path  = std::wstring(dir) + L"\\dxcompiler.dll";
        std::wstring dxil_path = std::wstring(dir) + L"\\dxil.dll";

        h_dxil = LoadLibraryExW(dxil_path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h_dxil) {
            std::fprintf(stderr, "[DedSec DXC] Failed to load dxil.dll from %ls\n", dir);
            return false;
        }

        h_dxc = LoadLibraryExW(dxc_path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h_dxc) {
            std::fprintf(stderr, "[DedSec DXC] Failed to load dxcompiler.dll from %ls\n", dir);
            FreeLibrary(h_dxil); h_dxil = nullptr;
            return false;
        }

        auto create_fn = reinterpret_cast<decltype(&DxcCreateInstance)>(
            GetProcAddress(h_dxc, "DxcCreateInstance"));
        if (!create_fn) {
            std::fprintf(stderr, "[DedSec DXC] dxcompiler.dll missing DxcCreateInstance\n");
            shutdown();
            return false;
        }

        HRESULT hr = create_fn(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.GetAddressOf()));
        if (FAILED(hr) || !compiler) {
            std::fprintf(stderr, "[DedSec DXC] DxcCreateInstance failed (hr=0x%08lX)\n",
                static_cast<unsigned long>(hr));
            shutdown();
            return false;
        }

        dxc_directory = dir;
        available = true;
        version_string = "DXC SM 6.6 (dynamic)";
        std::printf("[DedSec DXC] Loaded from %ls — ready\n", dir);
        return true;
    }
};

// ─── Singleton ────────────────────────────────────────────────────────────────

DxcCompiler& DxcCompiler::instance() {
    static DxcCompiler inst;
    return inst;
}

DxcCompiler::DxcCompiler()  : impl_(new Impl()) {}
DxcCompiler::~DxcCompiler() { delete impl_; }

bool DxcCompiler::set_dxc_directory(const std::string& path) {
    std::wstring wpath(path.begin(), path.end());
    return set_dxc_directory(wpath.c_str());
}

bool DxcCompiler::set_dxc_directory(const wchar_t* path) {
    return impl_->load_dlls(path);
}

bool DxcCompiler::is_available() const { return impl_->available; }
std::string DxcCompiler::get_version() const { return impl_->version_string; }
void DxcCompiler::shutdown() { impl_->shutdown(); }

void DxcCompiler::clear_cache() {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    impl_->cache.clear();
    impl_->cache_hits = 0;
    impl_->cache_misses = 0;
}

size_t DxcCompiler::get_cache_size() const {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    return impl_->cache.size();
}

uint64_t DxcCompiler::get_cache_hits()   const { return impl_->cache_hits; }
uint64_t DxcCompiler::get_cache_misses() const { return impl_->cache_misses; }

// ─── Compilation ──────────────────────────────────────────────────────────────

DxcCompileResult DxcCompiler::compile(
    const std::string& hlsl_source,
    DxcShaderStage stage,
    const DxcCompileOptions& options)
{
    return compile(hlsl_source, options.entry_point, stage, options);
}

DxcCompileResult DxcCompiler::compile(
    const std::string& hlsl_source,
    const std::string& entry_point,
    DxcShaderStage stage,
    const DxcCompileOptions& options)
{
    DxcCompileResult result{};
    result.source_hash = hash_string(hlsl_source + entry_point + get_dxc_target_profile(stage));

    // Check cache
    {
        std::lock_guard<std::mutex> lock(impl_->cache_mutex);
        auto it = impl_->cache.find(result.source_hash);
        if (it != impl_->cache.end()) {
            result.success         = true;
            result.dxil_blob       = it->second.dxil;
            result.error_message   = it->second.errors;
            result.compile_time_us = it->second.compile_time_us;
            impl_->cache_hits++;
            return result;
        }
    }

    if (!impl_->available) {
        result.success = false;
        result.error_message = "DXC not initialized — call set_dxc_directory() first";
        return result;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    const char* profile = get_dxc_target_profile(stage);

    // Build wide-string argument list
    auto to_wide = [](const std::string& s) -> std::wstring {
        return std::wstring(s.begin(), s.end());
    };

    std::vector<std::wstring> wargs_storage;
    std::vector<LPCWSTR> args;

    auto add_arg = [&](const std::wstring& w) {
        wargs_storage.push_back(w);
        args.push_back(wargs_storage.back().c_str());
    };

    add_arg(L"-E");
    add_arg(to_wide(entry_point));
    add_arg(L"-T");
    add_arg(to_wide(profile));
    add_arg(L"-HV");
    add_arg(L"2021");

    if (options.skip_optimizations) {
        add_arg(L"-Od");
    } else {
        add_arg(to_wide("-O" + std::to_string(std::min(options.optimization_level, 3u))));
    }

    if (options.warnings_as_errors) add_arg(L"-WX");
    if (options.debug_info)         add_arg(L"-Zi");

    for (const auto& def : options.defines) {
        add_arg(to_wide("-D" + def));
    }

    // Source buffer
    DxcBuffer source_buf{};
    source_buf.Ptr      = hlsl_source.data();
    source_buf.Size     = hlsl_source.size();
    source_buf.Encoding = DXC_CP_UTF8;

    ComPtr<IDxcResult> dxc_result;
    HRESULT hr = impl_->compiler->Compile(
        &source_buf,
        args.data(),
        static_cast<UINT32>(args.size()),
        nullptr,  // no include handler
        IID_PPV_ARGS(dxc_result.GetAddressOf()));

    if (FAILED(hr) || !dxc_result) {
        result.success = false;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(hr));
        result.error_message = std::string("IDxcCompiler3::Compile failed (hr=0x") + buf + ")";
        auto t1 = std::chrono::high_resolution_clock::now();
        result.compile_time_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        return result;
    }

    // Check compilation status
    HRESULT compile_status = S_OK;
    dxc_result->GetStatus(&compile_status);

    // Capture error/warning output
    {
        ComPtr<IDxcBlobUtf8> err_blob;
        if (SUCCEEDED(dxc_result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(err_blob.GetAddressOf()), nullptr))
            && err_blob && err_blob->GetStringLength() > 0) {
            result.error_message.assign(err_blob->GetStringPointer(), err_blob->GetStringLength());
        }
    }

    if (FAILED(compile_status)) {
        result.success = false;
        auto t1 = std::chrono::high_resolution_clock::now();
        result.compile_time_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        return result;
    }

    // Get compiled DXIL object
    {
        ComPtr<IDxcBlob> obj_blob;
        if (SUCCEEDED(dxc_result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(obj_blob.GetAddressOf()), nullptr))
            && obj_blob && obj_blob->GetBufferSize() > 0) {
            auto* ptr = reinterpret_cast<const uint8_t*>(obj_blob->GetBufferPointer());
            result.dxil_blob.assign(ptr, ptr + obj_blob->GetBufferSize());
        }
    }

    // Get PDB if requested
    if (options.debug_info) {
        ComPtr<IDxcBlob> pdb_blob;
        if (SUCCEEDED(dxc_result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pdb_blob.GetAddressOf()), nullptr))
            && pdb_blob && pdb_blob->GetBufferSize() > 0) {
            auto* ptr = reinterpret_cast<const uint8_t*>(pdb_blob->GetBufferPointer());
            result.pdb_blob.assign(ptr, ptr + pdb_blob->GetBufferSize());
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.compile_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    result.success = !result.dxil_blob.empty();

    if (!result.success) {
        result.error_message += "\nNo DXIL object produced";
    }

    // Store in cache
    if (result.success) {
        std::lock_guard<std::mutex> lock(impl_->cache_mutex);
        impl_->cache[result.source_hash] = {result.dxil_blob, result.error_message, result.compile_time_us};
        impl_->cache_misses++;
    }

    return result;
}

DxcCompileResult DxcCompiler::compile_file(
    const std::string& hlsl_path,
    DxcShaderStage stage,
    const DxcCompileOptions& options)
{
    std::ifstream file(hlsl_path, std::ios::binary);
    if (!file.is_open()) {
        DxcCompileResult result{};
        result.success = false;
        result.error_message = "Cannot open file: " + hlsl_path;
        return result;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return compile(ss.str(), stage, options);
}

} // namespace dedsec