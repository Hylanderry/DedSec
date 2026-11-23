// DedSec DXC Runtime Implementation — In-process HLSL → DXIL compilation
// Loads dxcompiler.dll at runtime, compiles with memory gating and disk cache.
// Adapted from XCL gpu/recomp/dxc.rs logic, rewritten in C++ without attribution.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "dedsec_dxc_runtime.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <combaseapi.h>
#include <dxcapi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

// Global state
static std::once_flag g_init_flag;
static HMODULE g_dxc_module = nullptr;
static HMODULE g_dxil_module = nullptr;
static DxcCreateInstanceProc g_create_instance = nullptr;
static std::string g_dxc_location;
static bool g_available = false;

// Memory gating
static std::atomic<int> g_working_compiles{0};
constexpr uint64_t kMemoryThreshold = 6ULL << 30; // 6 GB

// Cache statistics
static std::atomic<uint64_t> g_cache_hits{0};
static std::atomic<uint64_t> g_cache_misses{0};
static std::atomic<uint64_t> g_compile_errors{0};
static std::atomic<uint64_t> g_total_compile_ms{0};

// FNV-1a 64-bit hash
uint64_t fnv1a_hash(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string to_hex(uint64_t value) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buf);
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &ws[0], len);
    return ws;
}

bool file_exists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    auto size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return f.good();
}

void try_load_dxc() {
    std::vector<std::string> candidates;

    // 1. Environment variable
    char env_buf[MAX_PATH];
    if (GetEnvironmentVariableA("DEDSEC_DXC_DIR", env_buf, MAX_PATH) > 0) {
        candidates.push_back(std::string(env_buf));
    }

    // 2. Exe directory
    char exe_buf[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe_buf, MAX_PATH) > 0) {
        std::string exe_dir(exe_buf);
        auto pos = exe_dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            candidates.push_back(exe_dir.substr(0, pos));
        }
    }

    // 3. Windows SDK (newest version first)
    for (const char* root : {"C:\\Program Files (x86)\\Windows Kits\\10\\bin",
                              "C:\\Program Files\\Windows Kits\\10\\bin"}) {
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA((std::string(root) + "\\*").c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;

        std::vector<std::string> versions;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                fd.cFileName[0] != '.' && std::strlen(fd.cFileName) > 5) {
                versions.push_back(fd.cFileName);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);

        std::sort(versions.rbegin(), versions.rend());
        for (const auto& ver : versions) {
            candidates.push_back(std::string(root) + "\\" + ver + "\\x64");
        }
    }

    for (const auto& dir : candidates) {
        std::string dxc_path = dir + "\\dxcompiler.dll";
        std::string dxil_path = dir + "\\dxil.dll";

        if (!file_exists(dxc_path)) continue;

        if (file_exists(dxil_path)) {
            g_dxil_module = LoadLibraryA(dxil_path.c_str());
        }

        g_dxc_module = LoadLibraryA(dxc_path.c_str());
        if (!g_dxc_module) continue;

        g_create_instance = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(g_dxc_module, "DxcCreateInstance"));
        if (!g_create_instance) {
            FreeLibrary(g_dxc_module);
            g_dxc_module = nullptr;
            continue;
        }

        g_dxc_location = dir;
        g_available = true;
        return;
    }
}

} // anonymous namespace

namespace dedsec {
namespace dxc_runtime {

bool init() {
    std::call_once(g_init_flag, try_load_dxc);
    return g_available;
}

bool is_available() {
    return g_available;
}

std::string get_location() {
    return g_dxc_location;
}

std::vector<uint8_t> compile(const std::string& source,
                             const std::string& entry,
                             const std::string& profile,
                             const std::vector<std::string>& extra_args,
                             std::string* out_error) {
    if (!g_available || !g_create_instance) {
        if (out_error) *out_error = "DXC not available";
        return {};
    }

    IDxcCompiler3* compiler = nullptr;
    HRESULT hr = g_create_instance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3),
                                   reinterpret_cast<void**>(&compiler));
    if (FAILED(hr) || !compiler) {
        if (out_error) *out_error = "Failed to create DXC compiler instance";
        return {};
    }

    DxcBuffer src_buffer{};
    src_buffer.Ptr = source.data();
    src_buffer.Size = source.size();
    src_buffer.Encoding = DXC_CP_UTF8;

    std::vector<std::wstring> wide_args;
    wide_args.push_back(to_wide("-E"));
    wide_args.push_back(to_wide(entry));
    wide_args.push_back(to_wide("-T"));
    wide_args.push_back(to_wide(profile));
    wide_args.push_back(L"-O3");
    wide_args.push_back(L"-Zpr");

    for (const auto& arg : extra_args) {
        wide_args.push_back(to_wide(arg));
    }

    std::vector<LPCWSTR> arg_ptrs;
    for (const auto& wa : wide_args) {
        arg_ptrs.push_back(wa.c_str());
    }

    auto start = std::chrono::steady_clock::now();

    void* result_void = nullptr;
    hr = compiler->Compile(&src_buffer, arg_ptrs.data(),
                           static_cast<UINT32>(arg_ptrs.size()),
                           nullptr,
                           __uuidof(IDxcResult),
                           &result_void);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    g_total_compile_ms.fetch_add(static_cast<uint64_t>(elapsed), std::memory_order_relaxed);

    if (FAILED(hr) || !result_void) {
        compiler->Release();
        g_compile_errors.fetch_add(1, std::memory_order_relaxed);
        if (out_error) *out_error = "DXC Compile() failed";
        return {};
    }

    auto* result = static_cast<IDxcResult*>(result_void);

    HRESULT compile_status;
    result->GetStatus(&compile_status);
    if (FAILED(compile_status)) {
        IDxcBlobEncoding* errors = nullptr;
        result->GetErrorBuffer(&errors);
        if (errors) {
            LPVOID err_data = errors->GetBufferPointer();
            SIZE_T err_size = errors->GetBufferSize();
            if (err_data && err_size > 0) {
                if (out_error) *out_error = std::string(static_cast<const char*>(err_data), err_size);
            }
            errors->Release();
        } else {
            if (out_error) *out_error = "Compilation failed (no error details)";
        }
        result->Release();
        compiler->Release();
        g_compile_errors.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    // GetOutput takes DXC_OUT_KIND, REFIID, void**, IDxcBlobWide** (4 args)
    void* dxil_blob_void = nullptr;
    hr = result->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob),
                           &dxil_blob_void, nullptr);
    if (FAILED(hr) || !dxil_blob_void) {
        result->Release();
        compiler->Release();
        g_compile_errors.fetch_add(1, std::memory_order_relaxed);
        if (out_error) *out_error = "Failed to get DXIL output";
        return {};
    }

    auto* dxil_blob = static_cast<IDxcBlob*>(dxil_blob_void);
    LPVOID data = dxil_blob->GetBufferPointer();
    SIZE_T size = dxil_blob->GetBufferSize();

    std::vector<uint8_t> dxil;
    if (data && size > 0) {
        dxil.assign(static_cast<uint8_t*>(data),
                    static_cast<uint8_t*>(data) + size);
    }

    dxil_blob->Release();
    result->Release();
    compiler->Release();

    return dxil;
}

std::vector<uint8_t> compile_cached(const std::string& source,
                                    const std::string& entry,
                                    const std::string& profile,
                                    const std::string& cache_dir,
                                    const std::vector<std::string>& extra_args,
                                    std::string* out_error) {
    std::string key_data = source + "\0" + entry + "\0" + profile;
    for (const auto& arg : extra_args) {
        key_data += "\0" + arg;
    }
    uint64_t hash = fnv1a_hash(key_data);
    std::string cache_path = cache_dir + "\\" + to_hex(hash) + ".dxil";

    if (file_exists(cache_path)) {
        auto cached = read_file(cache_path);
        if (!cached.empty()) {
            g_cache_hits.fetch_add(1, std::memory_order_relaxed);
            return cached;
        }
    }

    g_cache_misses.fetch_add(1, std::memory_order_relaxed);

    auto dxil = compile(source, entry, profile, extra_args, out_error);
    if (dxil.empty()) return {};

    std::filesystem::create_directories(cache_dir);
    write_file(cache_path, dxil);

    return dxil;
}

MemorySlot::~MemorySlot() {
    if (acquired_) {
        g_working_compiles.fetch_sub(1, std::memory_order_acq_rel);
    }
}

MemorySlot::MemorySlot(MemorySlot&& other) noexcept : acquired_(other.acquired_) {
    other.acquired_ = false;
}

MemorySlot& MemorySlot::operator=(MemorySlot&& other) noexcept {
    if (this != &other) {
        if (acquired_) {
            g_working_compiles.fetch_sub(1, std::memory_order_acq_rel);
        }
        acquired_ = other.acquired_;
        other.acquired_ = false;
    }
    return *this;
}

MemorySlot acquire_memory_slot() {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        MEMORYSTATUSEX mem_status{};
        mem_status.dwLength = sizeof(mem_status);
        bool plenty = GlobalMemoryStatusEx(&mem_status) == 0 ||
                      mem_status.ullAvailPageFile >= kMemoryThreshold;

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();

        if (plenty || elapsed >= 60) {
            g_working_compiles.fetch_add(1, std::memory_order_acq_rel);
            MemorySlot slot;
            slot.acquired_ = true;
            return slot;
        }

        Sleep(100);
    }
}

CacheStats get_cache_stats() {
    return {
        g_cache_hits.load(std::memory_order_relaxed),
        g_cache_misses.load(std::memory_order_relaxed),
        g_compile_errors.load(std::memory_order_relaxed),
        g_total_compile_ms.load(std::memory_order_relaxed)
    };
}

} // namespace dxc_runtime
} // namespace dedsec