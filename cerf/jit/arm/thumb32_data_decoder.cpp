#include "thumb32_decoder.h"

#include "arm_decoder.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

namespace {

uint32_t ThumbDpOpcode(uint32_t op) {
    switch ((op >> 21) & 0xFu) {
    case 0u:  return 0u;   /* AND */
    case 1u:  return 14u;  /* BIC */
    case 2u:  return 12u;  /* ORR */
    case 3u:  return 15u;  /* ORN / MVN */
    case 4u:  return 1u;   /* EOR */
    case 8u:  return 4u;   /* ADD */
    case 10u: return 5u;   /* ADC */
    case 11u: return 6u;   /* SBC */
    case 13u: return 2u;   /* SUB */
    case 14u: return 3u;   /* RSB */
    default:  return 0xFFFFFFFFu;
    }
}

bool ValidLongRegs(uint32_t rn, uint32_t rm, uint32_t lo, uint32_t hi) {
    return rn != ArmGpr::kR15 && rm != ArmGpr::kR15 &&
           lo != ArmGpr::kR15 && hi != ArmGpr::kR15 && lo != hi;
}

}  /* namespace */

/* ARM DDI 0406C.c Table A6-12 (p. A6-234): MOVW/MOVT and ADDW/SUBW. */
bool Thumb32Decoder::DecodeDataProcessingPlainBinaryImmediate(
        DecodedInsn* insn, uint32_t op) {
    /* ARM DDI 0406C.c Table A6-12: BFI/BFC and SBFX/UBFX are members of
       the plain-binary-immediate class, despite being grouped with system
       instructions by the legacy 6.6 probe order. */
    if ((op & 0xFFF08020u) == 0xF3600000u) {
        const uint32_t rn = (op >> 16) & 0xFu;
        const uint32_t rd = (op >> 8) & 0xFu;
        const uint32_t lsb = (((op >> 12) & 7u) << 2) |
                             ((op >> 6) & 3u);
        const uint32_t msb = op & 0x1Fu;
        if (rd == ArmGpr::kR15 || msb < lsb) return false;
        const uint32_t width = msb - lsb + 1u;
        insn->rd = rd;
        insn->rn = rn;
        insn->op1 = lsb;
        insn->rs = width;
        insn->immediate = width == 32u ? 0xFFFFFFFFu :
                          (((1u << width) - 1u) << lsb);
        insn->place_fn = rn == ArmGpr::kR15 ? &PlaceBfc : &PlaceBfi;
        return true;
    }
    if (((op & 0xFFF08020u) == 0xF3400000u) ||
        ((op & 0xFFF08020u) == 0xF3C00000u)) {
        const uint32_t rn = (op >> 16) & 0xFu;
        const uint32_t rd = (op >> 8) & 0xFu;
        const uint32_t lsb = (((op >> 12) & 7u) << 2) |
                             ((op >> 6) & 3u);
        const uint32_t width = (op & 0x1Fu) + 1u;
        if (rn == ArmGpr::kR15 || rd == ArmGpr::kR15 ||
            lsb + width > 32u) return false;
        insn->rn = rn;
        insn->rd = rd;
        insn->op1 = lsb;
        insn->rs = width;
        insn->place_fn = (op & 0x00800000u) == 0u ? &PlaceSbfx : &PlaceUbfx;
        return true;
    }
    if (((op & 0xFBF08000u) == 0xF2400000u) ||
        ((op & 0xFBF08000u) == 0xF2C00000u)) {
        const uint32_t rd = (op >> 8) & 0xFu;
        if (rd == ArmGpr::kR15) return false;
        insn->rd = rd;
        insn->immediate = (((op >> 16) & 0xFu) << 12) |
                          (((op >> 26) & 1u) << 11) |
                          (((op >> 12) & 7u) << 8) | (op & 0xFFu);
        insn->place_fn = (op & 0x00800000u) == 0u ? &PlaceMovw : &PlaceMovt;
        return true;
    }
    if (((op & 0xFBE08000u) == 0xF2000000u) ||
        ((op & 0xFBE08000u) == 0xF2A00000u)) {
        const bool sub = (op & 0x00800000u) != 0u;
        const uint32_t rn = (op >> 16) & 0xFu;
        const uint32_t rd = (op >> 8) & 0xFu;
        if (rd == ArmGpr::kR15) return false;
        uint32_t imm = (((op >> 26) & 1u) << 11) |
                       (((op >> 12) & 7u) << 8) | (op & 0xFFu);
        if (rn == ArmGpr::kR15) {
            const uint32_t skew = (insn->guest_address + 4u) & 3u;
            imm = sub ? imm + skew : imm - skew;
        }
        insn->op1       = sub ? 2u : 4u;
        insn->rn        = rn;
        insn->rd        = rd;
        insn->immediate = imm;
        insn->place_fn  = &PlaceDataProcessing;
        return true;
    }
    Unimplemented("data-processing (plain binary immediate) (A6-234)",
                  insn, op);
}

/* ARM DDI 0406C.c Table A6-14 (p. A6-243): the Thumb T2 register form
   carries Rm/type/imm5 in the same logical shifter representation as ARM. */
bool Thumb32Decoder::DecodeDataProcessingShiftedRegister(DecodedInsn* insn,
                                                         uint32_t op) {
    uint32_t opcode = ThumbDpOpcode(op);
    const uint32_t rn = (op >> 16) & 0xFu;
    if (opcode == 0xFFFFFFFFu || (opcode == 15u && rn != ArmGpr::kR15)) {
        Unimplemented("data-processing (shifted register) (A6-243)", insn,
                      op);
    }
    const uint32_t rd = (op >> 8) & 0xFu;
    const uint32_t s  = (op >> 20) & 1u;
    const bool test = s != 0u && rd == ArmGpr::kR15 &&
                      (opcode == 0u || opcode == 1u ||
                       opcode == 2u || opcode == 4u);
    if (test) {
        opcode = opcode == 0u ? 8u : opcode == 1u ? 9u :
                 opcode == 2u ? 10u : 11u;
    } else if (opcode == 12u && rn == ArmGpr::kR15) {
        opcode = 13u;
    }
    uint32_t type = (op >> 4) & 3u;
    uint32_t amount = (((op >> 12) & 7u) << 2) | ((op >> 6) & 3u);
    if ((type == kSrLsr || type == kSrAsr) && amount == 0u) amount = 32u;
    if (type == kSrRor && amount == 0u) {
        type = kSrRrx;
        amount = 1u;
    }
    insn->op1 = opcode;
    insn->s   = s;
    insn->rn  = rn;
    insn->rd  = rd;
    insn->rm  = op & 0xFu;
    insn->n   = type;
    insn->rs  = amount;
    if (!test && rd == ArmGpr::kR15) {
        insn->r15_modified = true;
        insn->is_exception_return = s != 0u;
    }
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* ARM DDI 0406C.c Tables A6-15/A6-16 (pp. A6-245/A6-246). */
bool Thumb32Decoder::DecodeDataProcessingRegister(DecodedInsn* insn,
                                                  uint32_t op) {
    if ((op & 0xFFF0F0C0u) == 0xFA90F080u &&
        ((op >> 16) & 0xFu) == (op & 0xFu)) {
        const uint32_t kind = (op >> 4) & 3u;
        insn->place_fn = kind == 0u ? &PlaceRev : kind == 1u ? &PlaceRev16 :
                         kind == 2u ? &PlaceRbit : &PlaceRevsh;
        insn->rm = (op >> 16) & 0xFu;
        insn->rd = (op >> 8) & 0xFu;
        return true;
    }
    const uint32_t first_kind = (op >> 20) & 0xFFu;
    if ((first_kind == 0xA0u || first_kind == 0xA1u ||
         first_kind == 0xA4u || first_kind == 0xA5u) &&
        (op & 0xF0C0u) == 0xF080u) {
        const uint32_t rn = (op >> 16) & 0xFu;
        const bool byte = (first_kind & 4u) != 0u;
        const bool uns  = (first_kind & 1u) != 0u;
        insn->place_fn = rn == ArmGpr::kR15
            ? (byte ? (uns ? &PlaceUxtb : &PlaceSxtb)
                    : (uns ? &PlaceUxth : &PlaceSxth))
            : (byte ? (uns ? &PlaceUxtab : &PlaceSxtab)
                    : (uns ? &PlaceUxtah : &PlaceSxtah));
        insn->rn  = rn;
        insn->rd  = (op >> 8) & 0xFu;
        insn->rm  = op & 0xFu;
        insn->op1 = (op >> 4) & 3u;
        return true;
    }
    if ((op & 0xFF80F070u) == 0xFA00F000u) {
        insn->op1 = 13u;
        insn->s   = (op >> 20) & 1u;
        insn->n   = (op >> 21) & 3u;
        insn->rd  = (op >> 8) & 0xFu;
        insn->rm  = (op >> 16) & 0xFu;
        insn->rs  = op & 0xFu;
        insn->place_fn = &PlaceDataProcessingShiftedReg;
        return true;
    }
    if ((op & 0xFFF0F0F0u) == 0xFAB0F080u) {
        const uint32_t rm = (op >> 16) & 0xFu;
        const uint32_t rd = (op >> 8) & 0xFu;
        if (rm == ArmGpr::kR15 || rd == ArmGpr::kR15) return false;
        insn->rm = rm;
        insn->rd = rd;
        insn->place_fn = &PlaceClz;
        return true;
    }
    Unimplemented("data-processing (register) (A6-245)", insn, op);
}

/* ARM DDI 0406C.c Table A6-27 (p. A6-249): MUL/MLA/MLS. */
bool Thumb32Decoder::DecodeMultiplyAbsoluteDifference(DecodedInsn* insn,
                                                      uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >> 8) & 0xFu;
    const uint32_t rm = op & 0xFu;
    if ((op & 0xFFF000F0u) == 0xFB000000u) {
        const uint32_t word =
            (ra == ArmGpr::kR15 ? 0xE0000090u : 0xE0200090u) |
            (rd << 16) | ((ra == ArmGpr::kR15 ? 0u : ra) << 12) |
            (rm << 8) | rn;
        return arm_decoder_->DecodeArm(insn, ArmOpcode{word});
    }
    if ((op & 0xFFF000F0u) == 0xFB000010u) {
        const uint32_t word = 0xE0600090u | (rd << 16) | (ra << 12) |
                              (rm << 8) | rn;
        return arm_decoder_->DecodeArm(insn, ArmOpcode{word});
    }
    Unimplemented("multiply, multiply accumulate, absolute difference "
                  "(A6-249)", insn, op);
}

/* ARM DDI 0406C.c Table A6-28 (p. A6-250): the implemented 6.6 subset maps
   directly to the architecturally equivalent ARM multiply encodings. */
bool Thumb32Decoder::DecodeLongMultiplyDivide(DecodedInsn* insn, uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t lo = (op >> 12) & 0xFu;
    const uint32_t hi = (op >> 8) & 0xFu;
    const uint32_t rm = op & 0xFu;
    if (!ValidLongRegs(rn, rm, lo, hi)) return false;
    uint32_t base = 0u;
    if ((op & 0xFFF000F0u) == 0xFB800000u) base = 0xE0C00090u; /* SMULL */
    else if ((op & 0xFFF000F0u) == 0xFBA00000u) base = 0xE0800090u; /* UMULL */
    else if ((op & 0xFFF000F0u) == 0xFBC00000u) base = 0xE0E00090u; /* SMLAL */
    else if ((op & 0xFFF000F0u) == 0xFBE00000u) base = 0xE0A00090u; /* UMLAL */
    else if ((op & 0xFFF000F0u) == 0xFBE00060u) base = 0xE0400090u; /* UMAAL */
    if (base != 0u) {
        return arm_decoder_->DecodeArm(
            insn, ArmOpcode{base | (hi << 16) | (lo << 12) |
                            (rm << 8) | rn});
    }
    Unimplemented("long multiply, long multiply accumulate, divide (A6-250)",
                  insn, op);
}
