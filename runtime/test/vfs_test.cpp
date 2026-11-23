// DedSec Fork — VFS Layer Unit Tests
// Tests path normalization, overlay priority, archive integration, and thread safety.
// Uses synthetic RPF9 archives and temp directories; no game data required.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "../src/vfs.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

using namespace dedsec::vfs;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct TestReg_##name { \
        TestReg_##name() { \
            std::printf("  %-50s", #name); \
            try { \
                test_##name(); \
                std::printf("PASS\n"); \
                g_testsPassed++; \
            } catch (...) { \
                std::printf("FAIL (exception)\n"); \
                g_testsFailed++; \
            } \
        } \
    } g_testReg_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    if (!(expr)) { \
        std::fprintf(stderr, "\n  ASSERT FAILED: %s (line %d)\n", #expr, __LINE__); \
        throw 1; \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::fprintf(stderr, "\n  ASSERT FAILED: %s == %s (line %d)\n", #a, #b, __LINE__); \
        throw 1; \
    }

// ============================================================================
// Path Normalization Tests
// ============================================================================

TEST(NormalizePath_Separators) {
    std::string r = VirtualFileSystem::normalizePath("game:\\levels\\map.rpf", false);
    ASSERT_EQ(r, "levels/map.rpf");
}

TEST(NormalizePath_CaseInsensitive) {
    std::string r = VirtualFileSystem::normalizePath("GAME:/LEVELS/MAP.RPF", true);
    ASSERT_EQ(r, "levels/map.rpf");
}

TEST(NormalizePath_StripPrefixes) {
    ASSERT_EQ(VirtualFileSystem::normalizePath("game:/data/file.bin", false), "data/file.bin");
    ASSERT_EQ(VirtualFileSystem::normalizePath("device:/textures/t.dds", false), "textures/t.dds");
    ASSERT_EQ(VirtualFileSystem::normalizePath("title:/scripts/s.ysc", false), "scripts/s.ysc");
    ASSERT_EQ(VirtualFileSystem::normalizePath("update:/dlc/d.rpf", false), "dlc/d.rpf");
}

TEST(NormalizePath_CollapseSlashes) {
    std::string r = VirtualFileSystem::normalizePath("game:///data///file.bin", false);
    ASSERT_EQ(r, "data/file.bin");
}

TEST(NormalizePath_TrailingSlash) {
    std::string r = VirtualFileSystem::normalizePath("game:/data/", false);
    ASSERT_EQ(r, "data");
}

TEST(NormalizePath_NullAndEmpty) {
    ASSERT_EQ(VirtualFileSystem::normalizePath(nullptr, false), "");
    ASSERT_EQ(VirtualFileSystem::normalizePath("", false), "");
}

// ============================================================================
// Xbox→PC Path Mapping Tests
// ============================================================================

TEST(XboxToPcPath_Basic) {
    std::string r = VirtualFileSystem::xboxToPcPath("game:/levels/map.rpf", "C:/Games/GTA6");
    ASSERT_EQ(r, "C:/Games/GTA6/levels/map.rpf");
}

TEST(XboxToPcPath_NoTrailingSlash) {
    std::string r = VirtualFileSystem::xboxToPcPath("data/file.bin", "C:/Game");
    ASSERT_EQ(r, "C:/Game/data/file.bin");
}

TEST(XboxToPcPath_EmptyGameDir) {
    std::string r = VirtualFileSystem::xboxToPcPath("game:/data/file.bin", "");
    ASSERT_EQ(r, "data/file.bin");
}

// ============================================================================
// VFS Initialization Tests
// ============================================================================

TEST(Initialize_Basic) {
    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = ".";
    config.enableOverlay = false;

    ASSERT_TRUE(vfs.initialize(config));
    ASSERT_TRUE(vfs.isInitialized());

    // Double init should succeed (idempotent)
    ASSERT_TRUE(vfs.initialize(config));

    vfs.shutdown();
    ASSERT_TRUE(!vfs.isInitialized());
}

// ============================================================================
// Overlay Priority Tests
// ============================================================================

TEST(Overlay_PriorityOverFilesystem) {
    // Create temp directories
    MKDIR("vfs_test_overlay");
    MKDIR("vfs_test_base");

    // Write different content to overlay and base
    FILE* f1 = std::fopen("vfs_test_overlay/test.txt", "wb");
    std::fwrite("OVERLAY", 1, 7, f1);
    std::fclose(f1);

    FILE* f2 = std::fopen("vfs_test_base/test.txt", "wb");
    std::fwrite("BASE_DATA", 1, 9, f2);
    std::fclose(f2);

    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = "vfs_test_base";
    config.overlayDir = "vfs_test_overlay";
    config.enableOverlay = true;
    ASSERT_TRUE(vfs.initialize(config));

    VFSEntryInfo info{};
    ASSERT_TRUE(vfs.resolvePath("test.txt", info));
    ASSERT_TRUE(info.exists);
    ASSERT_TRUE(info.isOverlayEntry);
    ASSERT_EQ(info.size, 7u);

    // Read should return overlay content
    uint8_t buffer[32] = {};
    auto result = vfs.readFile("test.txt", buffer, sizeof(buffer));
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.bytesRead, 7u);
    ASSERT_TRUE(std::memcmp(buffer, "OVERLAY", 7) == 0);

    vfs.shutdown();
    std::remove("vfs_test_overlay/test.txt");
    std::remove("vfs_test_base/test.txt");
    _rmdir("vfs_test_overlay");
    _rmdir("vfs_test_base");
}

// ============================================================================
// Filesystem Fallback Tests
// ============================================================================

TEST(Filesystem_FallbackWhenNoOverlay) {
    MKDIR("vfs_test_fs");

    FILE* f = std::fopen("vfs_test_fs/plain.dat", "wb");
    std::fwrite("PLAIN", 1, 5, f);
    std::fclose(f);

    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = "vfs_test_fs";
    config.enableOverlay = false;
    ASSERT_TRUE(vfs.initialize(config));

    VFSEntryInfo info{};
    ASSERT_TRUE(vfs.resolvePath("plain.dat", info));
    ASSERT_TRUE(info.exists);
    ASSERT_TRUE(!info.isOverlayEntry);
    ASSERT_TRUE(!info.isArchiveEntry);
    ASSERT_EQ(info.size, 5u);

    vfs.shutdown();
    std::remove("vfs_test_fs/plain.dat");
    _rmdir("vfs_test_fs");
}

TEST(FileExists_NotFound) {
    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = ".";
    config.enableOverlay = false;
    ASSERT_TRUE(vfs.initialize(config));

    ASSERT_TRUE(!vfs.fileExists("nonexistent_file_xyz_12345.bin"));

    vfs.shutdown();
}

// ============================================================================
// Archive Integration Tests
// ============================================================================

// Helper: build a minimal RPF9 archive file
static bool buildTestArchive(const char* path) {
    // Minimal valid RPF9: header + 1 entry + data
    uint8_t archive[64] = {};

    // Header (32 bytes)
    uint32_t magic = kRPF9Magic;
    std::memcpy(archive + 0, &magic, 4);
    uint32_t version = 9;
    std::memcpy(archive + 4, &version, 4);
    uint32_t entryCount = 1;
    std::memcpy(archive + 8, &entryCount, 4);
    uint32_t tocOffset = 32;
    std::memcpy(archive + 12, &tocOffset, 4);
    uint32_t tocSize = 16;
    std::memcpy(archive + 16, &tocSize, 4);
    uint32_t dataOffset = 48;
    std::memcpy(archive + 20, &dataOffset, 4);
    uint32_t dataSize = 4;
    std::memcpy(archive + 24, &dataSize, 4);

    // TOC entry (16 bytes at offset 32)
    uint32_t hash = RPF9Reader::joaatHash("archived.bin");
    std::memcpy(archive + 32, &hash, 4);
    uint32_t offset = 0;
    std::memcpy(archive + 36, &offset, 4);
    uint32_t compSize = 4;
    std::memcpy(archive + 40, &compSize, 4);
    uint32_t uncompSize = 4;
    std::memcpy(archive + 44, &uncompSize, 4);

    // Data (4 bytes at offset 48)
    archive[48] = 0xDE;
    archive[49] = 0xAD;
    archive[50] = 0xBE;
    archive[51] = 0xEF;

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(archive, 1, 52, f);
    std::fclose(f);
    return true;
}

TEST(Archive_MountAndLookup) {
    ASSERT_TRUE(buildTestArchive("vfs_test_archive.rpf"));

    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = ".";
    config.enableOverlay = false;
    ASSERT_TRUE(vfs.initialize(config));

    ASSERT_TRUE(vfs.mountArchive("vfs_test_archive.rpf"));
    ASSERT_EQ(vfs.archiveCount(), 1u);

    VFSEntryInfo info{};
    ASSERT_TRUE(vfs.resolvePath("archived.bin", info));
    ASSERT_TRUE(info.exists);
    ASSERT_TRUE(info.isArchiveEntry);
    ASSERT_EQ(info.size, 4u);

    vfs.shutdown();
    std::remove("vfs_test_archive.rpf");
}

TEST(Archive_ReadData) {
    ASSERT_TRUE(buildTestArchive("vfs_test_read.rpf"));

    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = ".";
    config.enableOverlay = false;
    ASSERT_TRUE(vfs.initialize(config));
    ASSERT_TRUE(vfs.mountArchive("vfs_test_read.rpf"));

    uint8_t buffer[16] = {};
    auto result = vfs.readFile("archived.bin", buffer, sizeof(buffer));
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.bytesRead, 4u);
    ASSERT_EQ(buffer[0], 0xDE);
    ASSERT_EQ(buffer[1], 0xAD);
    ASSERT_EQ(buffer[2], 0xBE);
    ASSERT_EQ(buffer[3], 0xEF);

    vfs.shutdown();
    std::remove("vfs_test_read.rpf");
}

TEST(Archive_Unmount) {
    ASSERT_TRUE(buildTestArchive("vfs_test_unmount.rpf"));

    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = ".";
    config.enableOverlay = false;
    ASSERT_TRUE(vfs.initialize(config));
    ASSERT_TRUE(vfs.mountArchive("vfs_test_unmount.rpf"));
    ASSERT_EQ(vfs.archiveCount(), 1u);

    ASSERT_TRUE(vfs.unmountArchive("vfs_test_unmount.rpf"));
    ASSERT_EQ(vfs.archiveCount(), 0u);

    // Should no longer find the archived file
    ASSERT_TRUE(!vfs.fileExists("archived.bin"));

    vfs.shutdown();
    std::remove("vfs_test_unmount.rpf");
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST(Stats_Tracking) {
    MKDIR("vfs_test_stats");
    FILE* f = std::fopen("vfs_test_stats/s.dat", "wb");
    std::fwrite("STATS", 1, 5, f);
    std::fclose(f);

    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = "vfs_test_stats";
    config.enableOverlay = false;
    ASSERT_TRUE(vfs.initialize(config));

    vfs.resetStats();

    VFSEntryInfo info{};
    vfs.resolvePath("s.dat", info);
    vfs.resolvePath("nonexistent.bin", info);

    auto stats = vfs.getStats();
    ASSERT_EQ(stats.pathResolutions, 2u);
    ASSERT_TRUE(stats.filesystemHits >= 1u);
    ASSERT_TRUE(stats.misses >= 1u);

    vfs.shutdown();
    std::remove("vfs_test_stats/s.dat");
    _rmdir("vfs_test_stats");
}

// ============================================================================
// Thread Safety Test
// ============================================================================

TEST(ThreadSafety_ConcurrentResolve) {
    MKDIR("vfs_test_mt");
    for (int i = 0; i < 4; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "vfs_test_mt/f%d.dat", i);
        FILE* f = std::fopen(name, "wb");
        std::fwrite("DATA", 1, 4, f);
        std::fclose(f);
    }

    VirtualFileSystem vfs;
    VFSConfig config;
    config.gameDir = "vfs_test_mt";
    config.enableOverlay = false;
    ASSERT_TRUE(vfs.initialize(config));

    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&vfs, &successCount, t]() {
            char path[32];
            std::snprintf(path, sizeof(path), "f%d.dat", t);
            for (int i = 0; i < 100; ++i) {
                VFSEntryInfo info{};
                if (vfs.resolvePath(path, info) && info.exists) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    ASSERT_EQ(successCount.load(), 400);

    vfs.shutdown();
    for (int i = 0; i < 4; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "vfs_test_mt/f%d.dat", i);
        std::remove(name);
    }
    _rmdir("vfs_test_mt");
}

// ============================================================================
// Global Singleton Test
// ============================================================================

TEST(GlobalVFS_Singleton) {
    VirtualFileSystem& a = getGlobalVFS();
    VirtualFileSystem& b = getGlobalVFS();
    ASSERT_TRUE(&a == &b);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("=== VFS Layer tests ===\n");

    std::printf("\n--- Results ---\n");
    std::printf("Passed: %d\n", g_testsPassed);
    std::printf("Failed: %d\n", g_testsFailed);

    return g_testsFailed > 0 ? 1 : 0;
}