// DedSec Fork — RDNA2 VINTRP + EXP + MTBUF + VOP3P Translator Header
// Translates the final ~59 instruction forms to complete gfx1030 ISA coverage:
//   VINTRP (8): vertex parameter interpolation
//   EXP (4): export position/param/color/null
//   MTBUF (12): typed buffer load/store with format conversion
//   VOP3P (29): packed 3-operand ALU (pk_fma/add/mul f16/i16/u16)
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include <cstdint>
#include <string>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── VINTRP Opcodes (gfx1030, 8 forms) ────────────────────────────────────────
enum class VintrpOpcode : uint8_t {
    V_INTERP_P1_F32       = 0x00,
    V_INTERP_P2_F32       = 0x01,
    V_INTERP_MOV_F32      = 0x02,
    V_INTERP_P1LV_F16     = 0x03,
    V_INTERP_P2LV_F16     = 0x04,
    V_INTERP_P1_F16       = 0x05,
    V_INTERP_P2_F16       = 0x06,
    V_INTERP_MOV_F16      = 0x07,
};

struct VintrpInstruction {
    VintrpOpcode opcode;
    uint8_t vdst;
    uint8_t src0;   // attribute index / lane selector
    uint8_t attr_chan; // channel (x/y/z/w)
    bool high;       // high half for f16 variants
};

// ── EXP Opcodes (gfx1030, 4 forms) ───────────────────────────────────────────
enum class ExpTarget : uint8_t {
    EXP_MRT        = 0x00, // MRT0-MRT7
    EXP_POS        = 0x0C, // POS0-POS3
    EXP_PARAM      = 0x20, // PARAM0-PARAM31
    EXP_NULL       = 0x3F, // null export (done flag)
};

struct ExpInstruction {
    ExpTarget target;
    uint8_t en;       // enable mask [3:0] — which components are valid
    uint8_t src0;
    uint8_t src1;
    uint8_t src2;
    uint8_t src3;
    bool done;        // last export in shader
    bool valid_mask;  // write valid mask
    bool compr;       // compressed mode (2 components per reg)
};

// ── MTBUF Opcodes (gfx1030, 12 forms) ───────────────────────────────────────
enum class MtbufOpcode : uint8_t {
    TBUFFER_LOAD_FORMAT_X      = 0x00,
    TBUFFER_LOAD_FORMAT_XY     = 0x01,
    TBUFFER_LOAD_FORMAT_XYZ    = 0x02,
    TBUFFER_LOAD_FORMAT_XYZW   = 0x03,
    TBUFFER_STORE_FORMAT_X     = 0x04,
    TBUFFER_STORE_FORMAT_XY    = 0x05,
    TBUFFER_STORE_FORMAT_XYZ   = 0x06,
    TBUFFER_STORE_FORMAT_XYZW  = 0x07,
    BUFFER_LOAD_FORMAT_X       = 0x08,
    BUFFER_LOAD_FORMAT_XY      = 0x09,
    BUFFER_LOAD_FORMAT_XYZ     = 0x0A,
    BUFFER_LOAD_FORMAT_XYZW    = 0x0B,
};

struct MtbufInstruction {
    MtbufOpcode opcode;
    uint8_t vdst;
    uint8_t vaddr;     // VGPR offset
    uint8_t srsrc;     // SGPR resource descriptor
    uint8_t soffset;   // SGPR or inline offset
    uint8_t format;    // data format (DFMT/NFMT)
    uint16_t offset;   // inline constant offset [12:0]
    bool idxen;        // index enable
    bool offen;        // offset enable
    bool glc;          // globally coherent
    bool slc;          // system level coherent
    bool tfe;          // texture fail enable
    bool lds;          // LDS direct write
};

// ── VOP3P Opcodes (gfx1030, 29 forms) ───────────────────────────────────────
enum class Vop3pOpcode : uint16_t {
    V_PK_FMA_F16    = 0x00,
    V_PK_MUL_F16    = 0x01,
    V_PK_ADD_F16    = 0x02,
    V_PK_SUB_F16    = 0x03,
    V_PK_MIN_F16    = 0x04,
    V_PK_MAX_F16    = 0x05,
    V_PK_FMA_I16    = 0x06,
    V_PK_MUL_I16    = 0x07,
    V_PK_ADD_I16    = 0x08,
    V_PK_SUB_I16    = 0x09,
    V_PK_MIN_I16    = 0x0A,
    V_PK_MAX_I16    = 0x0B,
    V_PK_FMA_U16    = 0x0C,
    V_PK_MUL_U16    = 0x0D,
    V_PK_ADD_U16    = 0x0E,
    V_PK_SUB_U16    = 0x0F,
    V_PK_MIN_U16    = 0x10,
    V_PK_MAX_U16    = 0x11,
    V_PK_FMAC_F16   = 0x12,
    V_PK_DOT2_F32_F16 = 0x13,
    V_PK_DOT4_F32_F8  = 0x14,
    V_PK_DOT8_F32_F4  = 0x15,
    V_PK_LERP_F16     = 0x16,
    V_PK_MOV_B16      = 0x17,
    V_PK_AND_B16      = 0x18,
    V_PK_OR_B16       = 0x19,
    V_PK_XOR_B16      = 0x1A,
    V_PK_NOT_B16      = 0x1B,
    V_PK_NEG_I16      = 0x1C,
};

struct Vop3pInstruction {
    Vop3pOpcode opcode;
    uint8_t vdst;
    uint8_t src0;
    uint8_t src1;
    uint8_t src2;
    bool neg_src0;
    bool neg_src1;
    bool neg_src2;
    bool clamp;
    bool opsel_hi[3]; // high-half select for each source
};

// Decode functions
VintrpInstruction decode_vintrp(uint32_t encoded);
ExpInstruction decode_exp(uint32_t encoded);
MtbufInstruction decode_mtbuf(uint64_t encoded);
Vop3pInstruction decode_vop3p(uint64_t encoded);

// Translate to HLSL SM 6.6
std::string translate_vintrp(const VintrpInstruction& instr);
std::string translate_exp(const ExpInstruction& instr);
std::string translate_mtbuf(const MtbufInstruction& instr);
std::string translate_vop3p(const Vop3pInstruction& instr);

// Detection
bool is_vintrp_instruction(uint32_t word);
bool is_exp_instruction(uint32_t word);
bool is_mtbuf_instruction(uint64_t word);
bool is_vop3p_instruction(uint64_t word);

} // namespace rdna2_to_hlsl
} // namespace dedsec