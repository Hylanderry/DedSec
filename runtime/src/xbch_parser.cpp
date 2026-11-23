// DedSec XBCH Parser Implementation
// Xbox Series shader container extraction: DXBC → XDAT → XBCH(zlib) → FSSM
// Adapted from XCL gpu/xbox.rs logic, rewritten in C++ without attribution
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "xbch_parser.h"

#include <algorithm>
#include <cstring>

namespace dedsec {

namespace {

constexpr uint32_t S_ENDPGM = 0xBF810000;
constexpr uint32_t S_CODE_END = 0xBF9F0000;
constexpr size_t MAX_FSSM = 64 << 20; // 64 MB decompressed limit

uint32_t rd32(const uint8_t* b, size_t len, size_t off) {
    if (off + 4 > len) return 0;
    return static_cast<uint32_t>(b[off]) |
           (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) |
           (static_cast<uint32_t>(b[off + 3]) << 24);
}

uint64_t rd64(const uint8_t* b, size_t len, size_t off) {
    if (off + 8 > len) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(b[off + i]) << (i * 8);
    return v;
}

// Find zlib stream start (0x78 signature) in buffer
size_t find_zlib_header(const uint8_t* data, size_t len) {
    for (size_t i = 0; i + 2 <= len; ++i) {
        if (data[i] == 0x78 && (data[i + 1] == 0x01 || data[i + 1] == 0x5E ||
                                 data[i + 1] == 0x9C || data[i + 1] == 0xDA)) {
            return i;
        }
    }
    return SIZE_MAX;
}

// Minimal DEFLATE stored-block decompressor (RFC 1951 BTYPE=0).
// XBCH FSSM data commonly uses zlib-wrapped stored blocks for uncompressed shader code.
// Compressed blocks (BTYPE=1,2) require Huffman decoding — skipped here; full inflate
// can be added later via miniz or stb_decompress if needed for compressed entries.
std::vector<uint8_t> decompress_deflate_stored(const uint8_t* src, size_t src_len,
                                                size_t expected_size) {
    // Skip zlib header (2 bytes: CMF + FLG)
    if (src_len < 6) return {};
    const uint8_t* p = src + 2;
    size_t remaining = src_len - 2;

    std::vector<uint8_t> out;
    out.reserve(expected_size);

    while (remaining >= 5) {
        uint8_t bfinal = p[0] & 1;
        uint8_t btype = (p[0] >> 1) & 3;
        p++;
        remaining--;

        if (btype != 0) {
            // Compressed block (Huffman) — not supported in minimal decoder
            break;
        }

        // Stored block: LEN(2) + NLEN(2) + data
        if (remaining < 4) break;
        uint16_t len = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
        uint16_t nlen = static_cast<uint16_t>(p[2]) | (static_cast<uint16_t>(p[3]) << 8);
        p += 4;
        remaining -= 4;

        if ((len ^ 0xFFFF) != nlen) break; // integrity check
        if (len > remaining) break;

        out.insert(out.end(), p, p + len);
        p += len;
        remaining -= len;

        if (bfinal) break;
    }

    return out;
}

// Parse FSSM from decompressed data
Fssm parse_fssm(const uint8_t* data, size_t len, size_t xbch_offset,
                uint32_t xbch_version, size_t compressed_size) {
    Fssm fssm;
    fssm.xbch_offset = xbch_offset;
    fssm.xbch_version = xbch_version;
    fssm.compressed_size = compressed_size;
    fssm.data.assign(data, data + len);

    if (len < 0x100) {
        fssm.error = "FSSM too small (< 0x100 bytes)";
        return fssm;
    }

    // Check magic
    if (std::memcmp(data, "FSSM", 4) != 0) {
        fssm.error = "Missing FSSM magic";
        return fssm;
    }

    fssm.version = rd32(data, len, 4);
    uint64_t code_off = rd64(data, len, 0xC0);
    uint64_t code_sz = rd64(data, len, 0xC8);

    // Code region from header pointer
    if (code_off > 0 && code_sz > 0 && code_off + code_sz <= len) {
        fssm.code = {static_cast<size_t>(code_off), static_cast<size_t>(code_sz)};
        fssm.code_from_header = true;
    } else {
        // Scan for RDNA2 code (s_code_end pattern)
        for (size_t i = 0x100; i + 4 <= len; i += 4) {
            uint32_t inst = rd32(data, len, i);
            if (inst == S_CODE_END || inst == S_ENDPGM) {
                size_t start = i;
                while (start > 0x100 && rd32(data, len, start - 4) != S_CODE_END) {
                    start -= 4;
                }
                size_t end = i + 4;
                while (end + 4 <= len && rd32(data, len, end) != S_CODE_END) {
                    end += 4;
                }
                if (end > start) {
                    fssm.code = {start, end - start};
                    fssm.code_from_header = false;
                    break;
                }
            }
        }
        if (!fssm.code) {
            fssm.error = "No code region found in FSSM";
        }
    }

    // Parse hardware stage descriptors at +0x100 every 0x88 bytes
    for (size_t off = 0x100; off + 8 <= len; off += 0x88) {
        uint64_t desc = rd64(data, len, off);
        if (desc == 0 || desc == 0xDEADBEEFDEADBEEFull) continue;

        uint8_t hw = static_cast<uint8_t>(desc & 0xFF);
        uint8_t api = static_cast<uint8_t>((desc >> 8) & 0xFF);

        uint64_t upper = desc >> 16;
        if (upper != 0xC00002 && upper != 0 && upper != 0xDEADBEEF) continue;

        HwStage stage;
        stage.offset = off;
        stage.hw = static_cast<HardwareStage>(hw);
        stage.api = api;
        fssm.stages.push_back(stage);
    }

    // Extract programs and data blocks from code region
    if (fssm.code) {
        auto [coff, clen] = *fssm.code;
        Program prog{coff, 0};
        bool in_data = false;

        for (size_t i = coff; i + 4 <= coff + clen; i += 4) {
            uint32_t inst = rd32(data, len, i);
            if (inst == S_ENDPGM) {
                if (!in_data) {
                    prog.length = i + 4 - prog.offset;
                    if (prog.length > 0) fssm.programs.push_back(prog);
                    in_data = true;
                } else {
                    Program db{prog.offset, i + 4 - prog.offset};
                    if (db.length > 0) fssm.data_blocks.push_back(db);
                    in_data = false;
                }
                prog = {i + 4, 0};
            }
        }
    }

    return fssm;
}

} // namespace

XbchParseResult parse_xbch(std::span<const uint8_t> blob) {
    XbchParseResult result;

    if (blob.size() < 0x20) {
        result.error = "Blob too small for DXBC header";
        return result;
    }

    // Find DXIL chunk
    size_t pos = 20;
    size_t dxil_off = 0;
    size_t dxil_size = 0;

    while (pos + 8 <= blob.size()) {
        char tag[5] = {};
        std::memcpy(tag, blob.data() + pos, 4);
        uint32_t chunk_size = rd32(blob.data(), blob.size(), pos + 4);

        if (std::strcmp(tag, "DXIL") == 0) {
            dxil_off = pos + 8;
            dxil_size = chunk_size;
            break;
        }
        pos += 8 + ((chunk_size + 3) & ~3u);
    }

    if (dxil_off == 0 || dxil_size < 0x18) {
        result.error = "No DXIL chunk found or too small";
        return result;
    }

    // Search for "XDAT" signature inside DXIL part
    size_t xdat_off = 0;
    for (size_t i = dxil_off; i + 4 <= dxil_off + dxil_size; i += 4) {
        if (rd32(blob.data(), blob.size(), i) == 0x54414458) { // "XDAT" LE
            xdat_off = i;
            break;
        }
    }

    if (xdat_off == 0) {
        result.error = "No XDAT signature found in DXIL chunk";
        return result;
    }

    uint32_t xdat_count = rd32(blob.data(), blob.size(), xdat_off + 4);
    uint32_t xdat_table_off = rd32(blob.data(), blob.size(), xdat_off + 8);

    if (xdat_count > 1000) {
        result.error = "XDAT count unreasonably large";
        return result;
    }

    // Parse XDAT entries
    size_t table_pos = xdat_off + xdat_table_off;
    for (uint32_t i = 0; i < xdat_count; ++i) {
        size_t entry_off = table_pos + i * 12;
        if (entry_off + 12 > blob.size()) break;

        XdatEntry entry;
        entry.id = rd32(blob.data(), blob.size(), entry_off);
        uint32_t rel_offset = rd32(blob.data(), blob.size(), entry_off + 4);
        entry.size = rd32(blob.data(), blob.size(), entry_off + 8);
        entry.offset = dxil_off + rel_offset;

        result.xdat_entries.push_back(entry);

        // Process XBCH entries
        if (entry.id == XDAT_XBCH && entry.offset + 0x18 <= blob.size()) {
            if (std::memcmp(blob.data() + entry.offset, "XBCH", 4) != 0) continue;

            uint32_t version = rd32(blob.data(), blob.size(), entry.offset + 4);
            uint32_t comp_size = rd32(blob.data(), blob.size(), entry.offset + 8);
            uint32_t raw_size = rd32(blob.data(), blob.size(), entry.offset + 12);

            if (raw_size > MAX_FSSM || comp_size > entry.size) continue;

            size_t zlib_start = entry.offset + 0x18;
            if (zlib_start >= blob.size()) continue;

            size_t actual_zlib = find_zlib_header(blob.data() + zlib_start,
                                                   blob.size() - zlib_start);
            if (actual_zlib == SIZE_MAX) continue;
            zlib_start += actual_zlib;

            auto decompressed = decompress_deflate_stored(blob.data() + zlib_start,
                                                           blob.size() - zlib_start,
                                                           raw_size);
            if (decompressed.empty()) continue;

            auto fssm = parse_fssm(decompressed.data(), decompressed.size(),
                                    entry.offset, version, comp_size);
            result.fssms.push_back(std::move(fssm));
        }
    }

    if (result.fssms.empty() && !result.xdat_entries.empty()) {
        result.error = "XDAT parsed but no valid XBCH/FSSM found";
    }

    return result;
}

std::string hw_stage_name(HardwareStage hw) {
    switch (hw) {
        case HardwareStage::LS: return "LS";
        case HardwareStage::HS: return "HS";
        case HardwareStage::ES: return "ES";
        case HardwareStage::GS: return "GS";
        case HardwareStage::VS: return "VS";
        case HardwareStage::PS: return "PS";
        case HardwareStage::CS: return "CS";
        case HardwareStage::NGG: return "NGG";
        default: return "unknown";
    }
}

std::string api_stage_name(uint8_t api) {
    switch (api) {
        case 0: return "VS";
        case 1: return "PS";
        case 2: return "GS";
        case 3: return "CS";
        case 6: return "MS";
        default: return "api#" + std::to_string(api);
    }
}

} // namespace dedsec