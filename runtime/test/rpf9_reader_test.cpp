// DedSec Fork — RPF9 Reader Unit Tests
// Tests header parsing, joaat hashing, entry lookup, and streaming interface.
// Uses synthetic RPF9 archives built in-memory; no game data required.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "../src/rpf9_reader.h"

#include <cstdio>
#include <cstring>
#include <vector>

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
        std::fprintf(stderr, "\n    ASSERT FAILED: %s (line %d)\n", #expr, __LINE__); \
        throw 1; \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::fprintf(stderr, "\n    ASSERT FAILED: %s == %s (line %d)\n", #a, #b, __LINE__); \
        throw 1; \
    }

// ============================================================================
// Joaat Hash Tests
// ============================================================================

TEST(JoaatHash_KnownValues) {
    // Known joaat hashes used by RAGE engine
    // "test" → 0x3F75CCC1 (verified against CodeWalker/OpenIV reference)
    uint32_t h1 = RPF9Reader::joaatHash("test");
    ASSERT_EQ(h1, 0x3F75CCC1u);

    // Case insensitive: "TEST" should equal "test"
    uint32_t h2 = RPF9Reader::joaatHash("TEST");
    ASSERT_EQ(h1, h2);

    // Backslash normalization: "a\\b" == "a/b"
    uint32_t h3 = RPF9Reader::joaatHash("a\\b");
    uint32_t h4 = RPF9Reader::joaatHash("a/b");
    ASSERT_EQ(h3, h4);
}

TEST(JoaatHash_EmptyAndNull) {
    ASSERT_EQ(RPF9Reader::joaatHash(""), 0u);
    ASSERT_EQ(RPF9Reader::joaatHash(nullptr), 0u);
}

TEST(JoaatHash_LengthVariant) {
    const char* str = "hello_world";
    uint32_t h1 = RPF9Reader::joaatHash(str);
    uint32_t h2 = RPF9Reader::joaatHash(str, std::strlen(str));
    ASSERT_EQ(h1, h2);

    // Partial length should differ
    uint32_t h3 = RPF9Reader::joaatHash(str, 5);
    ASSERT_TRUE(h1 != h3);
}

// ============================================================================
// Synthetic Archive Builder
// ============================================================================

// Build a minimal valid RPF9 archive in memory for testing.
// Layout: [Header 32B] [TOC entries] [Data area]
struct SyntheticArchive {
    std::vector<uint8_t> data;
    std::string path;

    SyntheticArchive() : path("synthetic_test.rpf") {}

    void build(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& entries) {
        data.clear();

        uint32_t entryCount = static_cast<uint32_t>(entries.size());
        uint32_t tocOffset = sizeof(RPF9Header);
        uint32_t tocSize = entryCount * 16; // 16 bytes per entry
        uint32_t dataOffset = tocOffset + tocSize;

        // Calculate total data size
        uint32_t totalDataSize = 0;
        for (const auto& e : entries) {
            totalDataSize += static_cast<uint32_t>(e.second.size());
        }

        // Write header
        RPF9Header header{};
        header.magic = kRPF9Magic;
        header.version = 9;
        header.entryCount = entryCount;
        header.tocOffset = tocOffset;
        header.tocSize = tocSize;
        header.dataOffset = dataOffset;
        header.dataSize = totalDataSize;
        header.flags = 0;

        data.resize(sizeof(header));
        std::memcpy(data.data(), &header, sizeof(header));

        // Write TOC entries
        uint32_t currentOffset = 0;
        for (const auto& e : entries) {
            uint8_t entryBuf[16] = {};
            std::memcpy(entryBuf + 0, &e.first, 4);              // nameHash
            std::memcpy(entryBuf + 4, &currentOffset, 4);         // offset
            uint32_t size = static_cast<uint32_t>(e.second.size());
            std::memcpy(entryBuf + 8, &size, 4);                  // compressedSize
            std::memcpy(entryBuf + 12, &size, 4);                 // uncompressedSize (same = not compressed)

            data.insert(data.end(), entryBuf, entryBuf + 16);
            currentOffset += size;
        }

        // Write data area
        for (const auto& e : entries) {
            data.insert(data.end(), e.second.begin(), e.second.end());
        }
    }

    bool writeToFile() const {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
        return true;
    }

    void cleanup() const {
        std::remove(path.c_str());
    }
};

// ============================================================================
// Header Parsing Tests
// ============================================================================

TEST(Open_ValidArchive) {
    SyntheticArchive synth;
    synth.build({
        {RPF9Reader::joaatHash("file1.txt"), {'H', 'e', 'l', 'l', 'o'}},
        {RPF9Reader::joaatHash("file2.dat"), {0x01, 0x02, 0x03}},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));
    ASSERT_TRUE(reader.isOpen());
    ASSERT_EQ(reader.header().magic, kRPF9Magic);
    ASSERT_EQ(reader.entryCount(), 2u);

    reader.close();
    ASSERT_TRUE(!reader.isOpen());

    synth.cleanup();
}

TEST(Open_InvalidMagic) {
    // Write garbage as header
    uint8_t garbage[32] = {};
    FILE* f = std::fopen("bad_magic.rpf", "wb");
    std::fwrite(garbage, 1, sizeof(garbage), f);
    std::fclose(f);

    RPF9Reader reader;
    ASSERT_TRUE(!reader.open("bad_magic.rpf"));
    ASSERT_TRUE(reader.getLastError() != nullptr);

    std::remove("bad_magic.rpf");
}

TEST(Open_NonexistentFile) {
    RPF9Reader reader;
    ASSERT_TRUE(!reader.open("nonexistent_file_12345.rpf"));
}

// ============================================================================
// Entry Lookup Tests
// ============================================================================

TEST(FindEntry_ByHash) {
    SyntheticArchive synth;
    uint32_t hash1 = RPF9Reader::joaatHash("alpha.bin");
    uint32_t hash2 = RPF9Reader::joaatHash("beta.bin");
    synth.build({
        {hash1, {0xAA, 0xBB}},
        {hash2, {0xCC, 0xDD, 0xEE}},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));

    const RPF9Entry* e1 = reader.findEntry(hash1);
    ASSERT_TRUE(e1 != nullptr);
    ASSERT_EQ(e1->compressedSize, 2u);

    const RPF9Entry* e2 = reader.findEntry(hash2);
    ASSERT_TRUE(e2 != nullptr);
    ASSERT_EQ(e2->compressedSize, 3u);

    // Non-existent hash
    const RPF9Entry* e3 = reader.findEntry(0xDEADBEEFu);
    ASSERT_TRUE(e3 == nullptr);

    reader.close();
    synth.cleanup();
}

TEST(FindEntry_ByPath) {
    SyntheticArchive synth;
    synth.build({
        {RPF9Reader::joaatHash("models/car.ydr"), {0x01, 0x02}},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));

    // Case-insensitive lookup
    const RPF9Entry* e = reader.findEntry("MODELS/CAR.YDR");
    ASSERT_TRUE(e != nullptr);
    ASSERT_EQ(e->compressedSize, 2u);

    reader.close();
    synth.cleanup();
}

// ============================================================================
// Read Entry Tests
// ============================================================================

TEST(ReadEntry_RawData) {
    SyntheticArchive synth;
    std::vector<uint8_t> payload = {0x10, 0x20, 0x30, 0x40, 0x50};
    synth.build({
        {RPF9Reader::joaatHash("data.raw"), payload},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));

    const RPF9Entry* entry = reader.findEntry("data.raw");
    ASSERT_TRUE(entry != nullptr);

    uint8_t buffer[16] = {};
    size_t bytesRead = reader.readEntry(*entry, buffer, sizeof(buffer));
    ASSERT_EQ(bytesRead, payload.size());
    ASSERT_TRUE(std::memcmp(buffer, payload.data(), payload.size()) == 0);

    reader.close();
    synth.cleanup();
}

TEST(ReadEntry_SmallBuffer) {
    SyntheticArchive synth;
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8};
    synth.build({
        {RPF9Reader::joaatHash("big.bin"), payload},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));

    const RPF9Entry* entry = reader.findEntry("big.bin");
    ASSERT_TRUE(entry != nullptr);

    // Read only first 3 bytes
    uint8_t buffer[3] = {};
    size_t bytesRead = reader.readEntry(*entry, buffer, 3);
    ASSERT_EQ(bytesRead, 3u);
    ASSERT_EQ(buffer[0], 1);
    ASSERT_EQ(buffer[2], 3);

    reader.close();
    synth.cleanup();
}

// ============================================================================
// Streaming Read Tests
// ============================================================================

TEST(StreamingRead_Callback) {
    SyntheticArchive synth;
    std::vector<uint8_t> payload(1024, 0xAB);
    synth.build({
        {RPF9Reader::joaatHash("stream.dat"), payload},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));

    const RPF9Entry* entry = reader.findEntry("stream.dat");
    ASSERT_TRUE(entry != nullptr);

    size_t totalReceived = 0;
    bool allMatched = true;

    auto result = reader.readEntryStreaming(*entry, [&](const uint8_t* data, size_t size) -> bool {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] != 0xAB) allMatched = false;
        }
        totalReceived += size;
        return true; // Continue
    });

    ASSERT_TRUE(result.success);
    ASSERT_EQ(totalReceived, payload.size());
    ASSERT_TRUE(allMatched);

    reader.close();
    synth.cleanup();
}

TEST(StreamingRead_EarlyStop) {
    SyntheticArchive synth;
    // Payload must exceed kChunkSize (64KB) to force multiple read iterations;
    // otherwise the entire payload arrives in one callback and early-stop can't trigger.
    std::vector<uint8_t> payload(128 * 1024, 0xCD);
    synth.build({
        {RPF9Reader::joaatHash("stop.dat"), payload},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));

    const RPF9Entry* entry = reader.findEntry("stop.dat");
    ASSERT_TRUE(entry != nullptr);

    size_t totalReceived = 0;
    auto result = reader.readEntryStreaming(*entry, [&](const uint8_t*, size_t size) -> bool {
        totalReceived += size;
        return totalReceived < 256; // Stop after receiving ~256 bytes
    });

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(totalReceived < payload.size());

    reader.close();
    synth.cleanup();
}

// ============================================================================
// Empty Archive Test
// ============================================================================

TEST(EmptyArchive) {
    SyntheticArchive synth;
    synth.build({});
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader;
    ASSERT_TRUE(reader.open(synth.path.c_str()));
    ASSERT_EQ(reader.entryCount(), 0u);

    const RPF9Entry* e = reader.findEntry("anything");
    ASSERT_TRUE(e == nullptr);

    reader.close();
    synth.cleanup();
}

// ============================================================================
// Move Semantics Test
// ============================================================================

TEST(MoveSemantics) {
    SyntheticArchive synth;
    synth.build({
        {RPF9Reader::joaatHash("move.txt"), {'M'}},
    });
    ASSERT_TRUE(synth.writeToFile());

    RPF9Reader reader1;
    ASSERT_TRUE(reader1.open(synth.path.c_str()));
    ASSERT_TRUE(reader1.isOpen());

    RPF9Reader reader2(std::move(reader1));
    ASSERT_TRUE(reader2.isOpen());
    ASSERT_TRUE(!reader1.isOpen()); // NOLINT: moved-from state
    ASSERT_EQ(reader2.entryCount(), 1u);

    reader2.close();
    synth.cleanup();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("=== RPF9 Reader tests ===\n");

    std::printf("\n--- Results ---\n");
    std::printf("Passed: %d\n", g_testsPassed);
    std::printf("Failed: %d\n", g_testsFailed);

    return g_testsFailed > 0 ? 1 : 0;
}