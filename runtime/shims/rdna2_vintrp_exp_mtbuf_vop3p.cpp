// DedSec Fork — RDNA2 VINTRP + EXP + MTBUF + VOP3P Translator Implementation
// Translates the final ~59 instruction forms to complete gfx1030 ISA coverage.
// Clean-room implementation based on AMD ISA documentation.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include "rdna2_vintrp_exp_mtbuf_vop3p.h"
#include <sstream>
#include <cstdio>

namespace dedsec {
namespace rdna2_to_hlsl {

// ── Detection ────────────────────────────────────────────────────────────────

bool is_vintrp_instruction(uint32_t word) {
 // VINTRP: [31:26] = 0x3E for gfx1030 (VOP2 encoding with specific opcodes)
 uint8_t enc = static_cast<uint8_t>((word >> 26) & 0x3F);
 return (enc == 0x3E);
}

bool is_exp_instruction(uint32_t word) {
 // EXP: [31:26] = 0x3F for gfx1030
 uint8_t enc = static_cast<uint8_t>((word >> 26) & 0x3F);
 return (enc == 0x3F);
}

bool is_mtbuf_instruction(uint64_t word) {
 // MTBUF: [63:56] = 0xE8..0xEB for gfx1030
 uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
 return (enc >= 0xE8 && enc <= 0xEB);
}

bool is_vop3p_instruction(uint64_t word) {
 // VOP3P: [63:56] = 0xD8..0xDB for gfx1030
 uint8_t enc = static_cast<uint8_t>((word >> 56) & 0xFF);
 return (enc >= 0xD8 && enc <= 0xDB);
}

// ── Decoding ─────────────────────────────────────────────────────────────────

VintrpInstruction decode_vintrp(uint32_t encoded) {
 VintrpInstruction instr = {};
 // VINTRP uses VOP2-like encoding: [25:17]=opcode, [16:9]=vdst, [8:0]=src0
 // attr_chan and high are encoded in upper bits or opcode sub-fields
 instr.opcode = static_cast<VintrpOpcode>((encoded >> 17) & 0x07);
 instr.vdst = static_cast<uint8_t>((encoded >> 9) & 0xFF);
 instr.src0 = static_cast<uint8_t>(encoded & 0xFF);
 instr.attr_chan = static_cast<uint8_t>((encoded >> 26) & 0x03);
 instr.high = ((encoded >> 28) & 1) != 0;
 return instr;
}

ExpInstruction decode_exp(uint32_t encoded) {
 ExpInstruction instr = {};
 // EXP encoding: [25:22]=target, [21:18]=en, [17]=done, [16]=valid_mask,
 // [15]=compr, [14:8]=src0-src3 indices packed
 instr.target = static_cast<ExpTarget>((encoded >> 22) & 0x0F);
 instr.en = static_cast<uint8_t>((encoded >> 18) & 0x0F);
 instr.done = ((encoded >> 17) & 1) != 0;
 instr.valid_mask = ((encoded >> 16) & 1) != 0;
 instr.compr = ((encoded >> 15) & 1) != 0;
 instr.src0 = static_cast<uint8_t>((encoded >> 12) & 0x07);
 instr.src1 = static_cast<uint8_t>((encoded >> 9) & 0x07);
 instr.src2 = static_cast<uint8_t>((encoded >> 6) & 0x07);
 instr.src3 = static_cast<uint8_t>((encoded >> 3) & 0x07);
 return instr;
}

MtbufInstruction decode_mtbuf(uint64_t encoded) {
 MtbufInstruction instr = {};
 // MTBUF encoding (gfx1030):
 // [63:56]=ENC, [55:49]=opcode, [48]=glc, [47]=slc, [46]=tfe, [45]=lds,
 // [44:41]=format, [40:33]=srsrc, [32:25]=soffset, [24:17]=vaddr,
 // [16:9]=vdst, [8:0]=offset[12:0] split across fields
 instr.opcode = static_cast<MtbufOpcode>((encoded >> 49) & 0x7F);
 instr.glc = ((encoded >> 48) & 1) != 0;
 instr.slc = ((encoded >> 47) & 1) != 0;
 instr.tfe = ((encoded >> 46) & 1) != 0;
 instr.lds = ((encoded >> 45) & 1) != 0;
 instr.format = static_cast<uint8_t>((encoded >> 41) & 0x0F);
 instr.srsrc = static_cast<uint8_t>((encoded >> 33) & 0xFF);
 instr.soffset = static_cast<uint8_t>((encoded >> 25) & 0xFF);
 instr.vaddr = static_cast<uint8_t>((encoded >> 17) & 0xFF);
 instr.vdst = static_cast<uint8_t>((encoded >> 9) & 0xFF);
 instr.offset = static_cast<uint16_t>(encoded & 0x1FFF);
 instr.idxen = ((encoded >> 40) & 1) != 0;
 instr.offen = ((encoded >> 39) & 1) != 0;
 return instr;
}

Vop3pInstruction decode_vop3p(uint64_t encoded) {
 Vop3pInstruction instr = {};
 // VOP3P encoding (gfx1030):
 // [63:56]=ENC(0xD8-0xDB), [55:49]=opcode, [48]=clamp,
 // [47:45]=neg_src2/neg_src1/neg_src0, [44:42]=opsel_hi[2:0],
 // [41:33]=src2, [32:24]=src1, [23:15]=src0, [14:7]=vdst
 instr.opcode = static_cast<Vop3pOpcode>((encoded >> 49) & 0x7F);
 instr.clamp = ((encoded >> 48) & 1) != 0;
 instr.neg_src2 = ((encoded >> 47) & 1) != 0;
 instr.neg_src1 = ((encoded >> 46) & 1) != 0;
 instr.neg_src0 = ((encoded >> 45) & 1) != 0;
 instr.opsel_hi[2] = ((encoded >> 44) & 1) != 0;
 instr.opsel_hi[1] = ((encoded >> 43) & 1) != 0;
 instr.opsel_hi[0] = ((encoded >> 42) & 1) != 0;
 instr.src2 = static_cast<uint8_t>((encoded >> 33) & 0x1FF);
 instr.src1 = static_cast<uint8_t>((encoded >> 24) & 0x1FF);
 instr.src0 = static_cast<uint8_t>((encoded >> 15) & 0x1FF);
 instr.vdst = static_cast<uint8_t>((encoded >> 7) & 0xFF);
 return instr;
}

// ── VINTRP → HLSL ───────────────────────────────────────────────────────────

std::string translate_vintrp(const VintrpInstruction& instr) {
 std::ostringstream out;
 int vd = static_cast<int>(instr.vdst);
 int s0 = static_cast<int>(instr.src0);
 int chan = static_cast<int>(instr.attr_chan);
 const char* chans[] = {"x", "y", "z", "w"};
 const char* ch = (chan < 4) ? chans[chan] : "x";

 switch (instr.opcode) {
 case VintrpOpcode::V_INTERP_P1_F32:
 out << "float v" << vd << " = interp_p1_f32(attr" << s0 << "." << ch << ", i_bary);";
 break;
 case VintrpOpcode::V_INTERP_P2_F32:
 out << "float v" << vd << " = interp_p2_f32(v" << vd << ", attr" << s0 << "." << ch << ", j_bary);";
 break;
 case VintrpOpcode::V_INTERP_MOV_F32:
 out << "float v" << vd << " = attr" << s0 << "." << ch << "; /* INTERP_MOV */";
 break;
 case VintrpOpcode::V_INTERP_P1LV_F16:
 out << "min16float v" << vd << " = f32tof16(interp_p1_f32(attr" << s0 << "." << ch << ", i_bary));";
 break;
 case VintrpOpcode::V_INTERP_P2LV_F16:
 out << "min16float v" << vd << " = f32tof16(interp_p2_f32(f16tof32(v" << vd << "), attr" << s0 << "." << ch << ", j_bary));";
 break;
 case VintrpOpcode::V_INTERP_P1_F16:
 if (instr.high)
 out << "uint v" << vd << " = (v" << vd << " & 0xFFFFu) | (f32tof16(interp_p1_f32(attr" << s0 << "." << ch << ", i_bary)) << 16u);";
 else
 out << "uint v" << vd << " = (v" << vd << " & 0xFFFF0000u) | f32tof16(interp_p1_f32(attr" << s0 << "." << ch << ", i_bary));";
 break;
 case VintrpOpcode::V_INTERP_P2_F16:
 if (instr.high)
 out << "uint v" << vd << " = (v" << vd << " & 0xFFFFu) | (f32tof16(interp_p2_f32(f16tof32((v" << vd << " >> 16u) & 0xFFFFu), attr" << s0 << "." << ch << ", j_bary)) << 16u);";
 else
 out << "uint v" << vd << " = (v" << vd << " & 0xFFFF0000u) | f32tof16(interp_p2_f32(f16tof32(v" << vd << " & 0xFFFFu), attr" << s0 << "." << ch << ", j_bary));";
 break;
 case VintrpOpcode::V_INTERP_MOV_F16:
 if (instr.high)
 out << "uint v" << vd << " = (v" << vd << " & 0xFFFFu) | (f32tof16(attr" << s0 << "." << ch << ") << 16u);";
 else
 out << "uint v" << vd << " = (v" << vd << " & 0xFFFF0000u) | f32tof16(attr" << s0 << "." << ch << ");";
 break;
 default:
 out << "/* UNSUPPORTED VINTRP opcode 0x"
 << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
 break;
 }
 return out.str();
}

// ── EXP → HLSL ──────────────────────────────────────────────────────────────

std::string translate_exp(const ExpInstruction& instr) {
 std::ostringstream out;
 int s0 = static_cast<int>(instr.src0);
 int s1 = static_cast<int>(instr.src1);
 int s2 = static_cast<int>(instr.src2);
 int s3 = static_cast<int>(instr.src3);

 if (instr.target == ExpTarget::EXP_NULL) {
 out << "/* EXP_NULL done=" << (instr.done ? 1 : 0) << " */";
 if (instr.done) out << " return;";
 return out.str();
 }

 // Determine target name
 std::string target_name;
 int target_idx = static_cast<int>(instr.target);
 if (target_idx >= 0x00 && target_idx <= 0x07) {
 target_name = "sv_target[" + std::to_string(target_idx) + "]";
 } else if (target_idx >= 0x0C && target_idx <= 0x0F) {
 target_name = "sv_position[" + std::to_string(target_idx - 0x0C) + "]";
 } else if (target_idx >= 0x20 && target_idx <= 0x3F) {
 target_name = "param[" + std::to_string(target_idx - 0x20) + "]";
 } else {
 target_name = "unknown_target_" + std::to_string(target_idx);
 }

 out << "/* EXP " << target_name << " en=0x" << std::hex << static_cast<int>(instr.en) << std::dec;
 if (instr.compr) out << " COMPR";
 if (instr.valid_mask) out << " VM";
 out << " */ ";

 // Emit per-component writes based on enable mask
 if (instr.en & 1) out << target_name << ".x = v" << s0 << "; ";
 if (instr.en & 2) out << target_name << ".y = v" << s1 << "; ";
 if (instr.en & 4) out << target_name << ".z = v" << s2 << "; ";
 if (instr.en & 8) out << target_name << ".w = v" << s3 << "; ";

 if (instr.done) out << "return; /* EXP done */";

 return out.str();
}

// ── MTBUF → HLSL ────────────────────────────────────────────────────────────

std::string translate_mtbuf(const MtbufInstruction& instr) {
 std::ostringstream out;
 int vd = static_cast<int>(instr.vdst);
 int va = static_cast<int>(instr.vaddr);
 int sr = static_cast<int>(instr.srsrc);
 int so = static_cast<int>(instr.soffset);
 int fmt = static_cast<int>(instr.format);

 // Component count from opcode
 int comps = 1;
 bool is_store = false;
 bool is_typed = true;
 switch (instr.opcode) {
 case MtbufOpcode::TBUFFER_LOAD_FORMAT_X: comps = 1; break;
 case MtbufOpcode::TBUFFER_LOAD_FORMAT_XY: comps = 2; break;
 case MtbufOpcode::TBUFFER_LOAD_FORMAT_XYZ: comps = 3; break;
 case MtbufOpcode::TBUFFER_LOAD_FORMAT_XYZW: comps = 4; break;
 case MtbufOpcode::TBUFFER_STORE_FORMAT_X: comps = 1; is_store = true; break;
 case MtbufOpcode::TBUFFER_STORE_FORMAT_XY: comps = 2; is_store = true; break;
 case MtbufOpcode::TBUFFER_STORE_FORMAT_XYZ: comps = 3; is_store = true; break;
 case MtbufOpcode::TBUFFER_STORE_FORMAT_XYZW: comps = 4; is_store = true; break;
 case MtbufOpcode::BUFFER_LOAD_FORMAT_X: comps = 1; is_typed = false; break;
 case MtbufOpcode::BUFFER_LOAD_FORMAT_XY: comps = 2; is_typed = false; break;
 case MtbufOpcode::BUFFER_LOAD_FORMAT_XYZ: comps = 3; is_typed = false; break;
 case MtbufOpcode::BUFFER_LOAD_FORMAT_XYZW: comps = 4; is_typed = false; break;
 default: break;
 }

 // Build offset expression
 std::string offset_expr;
 if (instr.idxen && instr.offen) {
 offset_expr = "v" + std::to_string(va) + " + s" + std::to_string(so) + " + " + std::to_string(instr.offset);
 } else if (instr.idxen) {
 offset_expr = "v" + std::to_string(va) + " + " + std::to_string(instr.offset);
 } else if (instr.offen) {
 offset_expr = "s" + std::to_string(so) + " + " + std::to_string(instr.offset);
 } else {
 offset_expr = std::to_string(instr.offset);
 }

 // Coherency flags
 std::string flags;
 if (instr.glc) flags += "GLC ";
 if (instr.slc) flags += "SLC ";
 if (instr.tfe) flags += "TFE ";
 if (instr.lds) flags += "LDS ";

 const char* type_str = "";
 switch (comps) {
 case 1: type_str = "float"; break;
 case 2: type_str = "float2"; break;
 case 3: type_str = "float3"; break;
 case 4: type_str = "float4"; break;
 }

 if (is_store) {
 out << "/* MTBUF STORE fmt=" << fmt << " " << flags << "*/ ";
 out << "buffer_store_fmt" << comps << "(res" << sr << ", " << offset_expr << ", v" << vd << ");";
 } else {
 out << type_str << " v" << vd << " = ";
 if (is_typed) {
 out << "tbuffer_load_fmt" << comps << "(res" << sr << ", " << offset_expr << ", fmt=" << fmt;
 } else {
 out << "buffer_load_fmt" << comps << "(res" << sr << ", " << offset_expr;
 }
 if (!flags.empty()) out << ", " << flags;
 out << ");";
 }

 return out.str();
}

// ── VOP3P → HLSL ────────────────────────────────────────────────────────────

static std::string pk_src(uint8_t src, bool neg, bool hi) {
 std::string result;
 if (neg) result += '-';
 result += hi ? "pk_hi(v" : "pk_lo(v";
 result += std::to_string(static_cast<int>(src));
 result += ')';
 return result;
}

std::string translate_vop3p(const Vop3pInstruction& instr) {
 std::ostringstream out;
 int vd = static_cast<int>(instr.vdst);
 std::string s0 = pk_src(instr.src0, instr.neg_src0, instr.opsel_hi[0]);
 std::string s1 = pk_src(instr.src1, instr.neg_src1, instr.opsel_hi[1]);
 std::string s2 = pk_src(instr.src2, instr.neg_src2, instr.opsel_hi[2]);

 switch (instr.opcode) {
 // ── Packed F16 ALU ─────────────────────────────────────────
 case Vop3pOpcode::V_PK_FMA_F16:
 out << "uint v" << vd << " = pk_fma_f16(" << s0 << ", " << s1 << ", " << s2 << ");";
 break;
 case Vop3pOpcode::V_PK_MUL_F16:
 out << "uint v" << vd << " = pk_mul_f16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_ADD_F16:
 out << "uint v" << vd << " = pk_add_f16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_SUB_F16:
 out << "uint v" << vd << " = pk_sub_f16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_MIN_F16:
 out << "uint v" << vd << " = pk_min_f16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_MAX_F16:
 out << "uint v" << vd << " = pk_max_f16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_FMAC_F16:
 out << "uint v" << vd << " = pk_fmac_f16(" << s0 << ", " << s1 << ", v" << vd << ");";
 break;
 case Vop3pOpcode::V_PK_LERP_F16:
 out << "uint v" << vd << " = pk_lerp_f16(" << s0 << ", " << s1 << ", " << s2 << ");";
 break;

 // ── Packed I16 ALU ─────────────────────────────────────────
 case Vop3pOpcode::V_PK_FMA_I16:
 out << "uint v" << vd << " = pk_fma_i16(" << s0 << ", " << s1 << ", " << s2 << ");";
 break;
 case Vop3pOpcode::V_PK_MUL_I16:
 out << "uint v" << vd << " = pk_mul_i16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_ADD_I16:
 out << "uint v" << vd << " = pk_add_i16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_SUB_I16:
 out << "uint v" << vd << " = pk_sub_i16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_MIN_I16:
 out << "uint v" << vd << " = pk_min_i16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_MAX_I16:
 out << "uint v" << vd << " = pk_max_i16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_NEG_I16:
 out << "uint v" << vd << " = pk_neg_i16(" << s0 << ");";
 break;

 // ── Packed U16 ALU ─────────────────────────────────────────
 case Vop3pOpcode::V_PK_FMA_U16:
 out << "uint v" << vd << " = pk_fma_u16(" << s0 << ", " << s1 << ", " << s2 << ");";
 break;
 case Vop3pOpcode::V_PK_MUL_U16:
 out << "uint v" << vd << " = pk_mul_u16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_ADD_U16:
 out << "uint v" << vd << " = pk_add_u16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_SUB_U16:
 out << "uint v" << vd << " = pk_sub_u16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_MIN_U16:
 out << "uint v" << vd << " = pk_min_u16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_MAX_U16:
 out << "uint v" << vd << " = pk_max_u16(" << s0 << ", " << s1 << ");";
 break;

 // ── Packed dot products ────────────────────────────────────
 case Vop3pOpcode::V_PK_DOT2_F32_F16:
 out << "float v" << vd << " = dot(f16tof32_2(" << s0 << "), f16tof32_2(" << s1 << "));";
 break;
 case Vop3pOpcode::V_PK_DOT4_F32_F8:
 out << "float v" << vd << " = dot4_f8(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_DOT8_F32_F4:
 out << "float v" << vd << " = dot8_f4(" << s0 << ", " << s1 << ");";
 break;

 // ── Packed bitwise ─────────────────────────────────────────
 case Vop3pOpcode::V_PK_MOV_B16:
 out << "uint v" << vd << " = pk_mov_b16(" << s0 << ");";
 break;
 case Vop3pOpcode::V_PK_AND_B16:
 out << "uint v" << vd << " = pk_and_b16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_OR_B16:
 out << "uint v" << vd << " = pk_or_b16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_XOR_B16:
 out << "uint v" << vd << " = pk_xor_b16(" << s0 << ", " << s1 << ");";
 break;
 case Vop3pOpcode::V_PK_NOT_B16:
 out << "uint v" << vd << " = pk_not_b16(" << s0 << ");";
 break;

 default:
 out << "/* UNSUPPORTED VOP3P opcode 0x"
 << std::hex << static_cast<int>(instr.opcode) << std::dec << " */";
 break;
 }

 if (instr.clamp) out << " /* clamp */";

 return out.str();
}

} // namespace rdna2_to_hlsl
} // namespace dedsec