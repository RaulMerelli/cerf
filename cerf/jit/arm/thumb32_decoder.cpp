#include "thumb32_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_coproc_space_decoder.h"
#include "arm_opcode.h"
#include "decoded_insn.h"
#include "neon_unconditional_decoder.h"
#include "place_fns.h"

REGISTER_SERVICE(Thumb32Decoder);

bool Thumb32Decoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32Decoder::OnReady() {
    has_thumb2_     = emu_.Get<ArmProcessorConfig>().HasThumb2();
    has_neon_       = emu_.Get<ArmProcessorConfig>().HasNeon();
    coproc_decoder_ = &emu_.Get<ArmCoprocSpaceDecoder>();
    neon_decoder_   = &emu_.Get<NeonUnconditionalDecoder>();
}

void Thumb32Decoder::Unimplemented(const char* what, const DecodedInsn* insn,
                                   uint32_t op) {
    emu_.Get<Fatal>().Die("Thumb32Decoder: %s not implemented, op=0x%08X "
                          "at guest PC 0x%08X\n",
                          what, op, insn->guest_address);
}

bool Thumb32Decoder::DecodeLoadStoreMultiple(DecodedInsn* insn, uint32_t op) {
    Unimplemented("load/store multiple (A6-237)", insn, op);
}

bool Thumb32Decoder::DecodeLoadStoreDualExclusiveTableBranch(DecodedInsn* insn,
                                                             uint32_t op) {
    Unimplemented("load/store dual, load/store exclusive, table branch "
                  "(A6-238)", insn, op);
}

/* ARM DDI 0406C.c "Move register and immediate shifts" and Table A6-23
   (A6.3.11, p. A6-244): type = bits[5:4] with imm3:imm2 select A8.8.103 MOV
   (register, Thumb) (p. A8-486), A8.8.94 LSL (immediate) (p. A8-468), A8.8.96
   LSR (immediate) (p. A8-472), A8.8.16 ASR (immediate) (p. A8-330), A8.8.151
   RRX (p. A8-572) and A8.8.149 ROR (immediate) (p. A8-568). The five shift
   rows carry "if d IN {13,15} || m IN {13,15} then UNPREDICTABLE"; MOV
   encoding T3 instead carries "if setflags && (d IN {13,15} || m IN {13,15})"
   and "if !setflags && (d == 15 || m == 15 || (d == 13 && m == 13))". */
bool Thumb32Decoder::DecodeMoveRegisterImmediateShifts(DecodedInsn* insn,
                                                       uint32_t op) {
    const uint32_t s    = (op >> 20) & 0x1u;
    const uint32_t imm3 = (op >> 12) & 0x7u;
    const uint32_t rd   = (op >>  8) & 0xFu;
    const uint32_t imm2 = (op >>  6) & 0x3u;
    const uint32_t type = (op >>  4) & 0x3u;
    const uint32_t rm   =  op        & 0xFu;
    const uint32_t imm5 = (imm3 << 2) | imm2;

    if (type == kSrLsl && imm5 == 0u && s == 0u) {
        if (rd == 0xFu || rm == 0xFu || (rd == 13u && rm == 13u)) {
            return false;
        }
    } else if (rd == 13u || rd == 0xFu || rm == 13u || rm == 0xFu) {
        return false;
    }

    uint32_t shift_t = 0u;
    uint32_t shift_n = 0u;
    DecodeImmShift(type, imm5, &shift_t, &shift_n);

    insn->op1      = 13u;
    insn->s        = s;
    insn->rn       = 0xFu;
    insn->rd       = rd;
    insn->rm       = rm;
    insn->n        = shift_t;
    insn->rs       = shift_n;
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* ARM DDI 0406C.c A6.3.11 and Table A6-22 (p. A6-243): op = bits[24:21],
   S = bit[20], Rn = bits[19:16], imm3 = bits[14:12], Rd = bits[11:8],
   imm2 = bits[7:6], type = bits[5:4], Rm = bits[3:0], with "(shift_t,
   shift_n) = DecodeImmShift(type, imm3:imm2)" (A8.8.14, p. A8-326);
   "Other encodings in this space are UNDEFINED". Across A8.8.14 AND
   (p. A8-326), A8.8.241 TST (p. A8-746), A8.8.22 BIC (p. A8-342), A8.8.123
   ORR (p. A8-518), A8.8.121 ORN (p. A8-514), A8.8.116 MVN (p. A8-506),
   A8.8.47 EOR (p. A8-384), A8.8.238 TEQ (p. A8-740), A8.8.6 ADD encoding T3
   (p. A8-310), A8.8.35 CMN (p. A8-366), A8.8.2 ADC (p. A8-302), A8.8.162 SBC
   (p. A8-594), A8.8.223 SUB (p. A8-712), A8.8.38 CMP encoding T3 (p. A8-372)
   and A8.8.153 RSB (p. A8-576): every row carries "m IN {13,15}". */
bool Thumb32Decoder::DecodeDataProcessingShiftedRegister(DecodedInsn* insn,
                                                         uint32_t op) {
    const uint32_t o    = (op >> 21) & 0xFu;
    const uint32_t s    = (op >> 20) & 0x1u;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t imm3 = (op >> 12) & 0x7u;
    const uint32_t rd   = (op >>  8) & 0xFu;
    const uint32_t imm2 = (op >>  6) & 0x3u;
    const uint32_t type = (op >>  4) & 0x3u;
    const uint32_t rm   =  op        & 0xFu;

    /* A6.1.1 (p. A6-220): "An instruction is UNPREDICTABLE if: a bit marked
       (0) in the encoding diagram of an instruction is not 0, and the
       pseudocode for that encoding does not indicate that a different special
       case applies when that bit is not 0". Every encoding this class reaches
       marks bits[15] of the second halfword (0) - A8.8.14 (p. A8-326) shows it
       for the shared T2 diagram - and Table A6-9 (p. A6-230) does not consume
       that bit on the op1 = 01 path. */
    if (((op >> 15) & 0x1u) != 0u) {
        return false;
    }

    /* A8.8.125 PKH (p. A8-522), encoding T1: "if S == '1' || T == '1' then
       UNDEFINED", S at bit[4] of the first halfword and T at bit[4] of the
       second. */
    if (o == 0x6u) {
        if (s != 0u || ((op >> 4) & 0x1u) != 0u) {
            return false;
        }
        return MarkArmUnimplemented(insn, op);
    }
    if (o == 0x2u && rn == 0xFu) {
        return DecodeMoveRegisterImmediateShifts(insn, op);
    }

    uint32_t opcode = 0u;
    bool     test   = false;
    if (!MapDataProcessingOpcode(o, rn, rd, s, &opcode, &test)) {
        return false;
    }

    if (rm == 13u || rm == 0xFu) {
        return false;
    }
    bool sp_form = false;
    if (!DataProcRegistersValid(o, rn, rd, test, &sp_form)) {
        return false;
    }

    uint32_t shift_t = 0u;
    uint32_t shift_n = 0u;
    DecodeImmShift(type, (imm3 << 2) | imm2, &shift_t, &shift_n);
    /* A8.8.10 ADD (SP plus register, Thumb) (p. A8-318) and A8.8.226 SUB (SP
       minus register) (p. A8-718), encodings T3 and T1: "if d == 13 &&
       (shift_t != SRType_LSL || shift_n > 3) then UNPREDICTABLE". */
    if (sp_form && rd == 13u && (shift_t != kSrLsl || shift_n > 3u)) {
        return false;
    }

    insn->op1      = opcode;
    insn->s        = s;
    insn->rn       = rn;
    insn->rd       = rd;
    insn->rm       = rm;
    insn->n        = shift_t;
    insn->rs       = shift_n;
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

bool Thumb32Decoder::DecodeBranchesMiscControl(DecodedInsn* insn, uint32_t op) {
    Unimplemented("branches and miscellaneous control (A6-235)", insn, op);
}

/* ARM DDI 0406C.c Table A6-30 (A6.3.18, p. A6-251) and Table A5-22 (A5.6,
   p. A5-215) place op1 at bits[25:20], coproc at bits[11:8] and op at bit[4]
   identically and agree row for row, except op1 = 11xxxx: Advanced SIMD in
   A6-30, Supervisor Call in A5-22. A7.4 (p. A7-261): the U bit "is bit[12] of
   the first halfword in the Thumb encoding, and bit[24] in the ARM encoding.
   Other variable bits are in identical locations". */
bool Thumb32Decoder::DecodeCoprocessorSimdFp(DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 20) & 0x3Fu;
    ArmOpcode      arm{};
    if ((op1 & 0x30u) == 0x30u) {
        if (!has_neon_) return false;
        arm.word = 0xF2000000u | (((op >> 28) & 0x1u) << 24) |
                   (op & 0x00FFFFFFu);
        neon_decoder_->DecodeData3reg(insn, arm);
        if (insn->place_fn == &PlaceNeonUnimplemented) insn->immediate = op;
        return true;
    }
    /* T is hw1[12]. A7.5 (p. A7-272), A7.6 (p. A7-274), A7.8 (p. A7-278) and
       A7.9 (p. A7-279): "If T == 1 in the Thumb encoding or cond == 0b1111 in
       the ARM encoding, the instruction is UNDEFINED", scoped to cp10 and
       cp11. B3.15.2 (p. B3-1446) makes "all CDP2, MCR2, MRC2, MCRR2, MRRC2,
       LDC2, LDCL, LDC2L, STC2, STCL and STC2L operations to CP14 and CP15"
       UNDEFINED. A2.9 (p. A2-94) reserves CP8, CP9, CP12 and CP13; B1.9.2
       (p. B1-1206) UNDEFs "a coprocessor instruction that is not
       implemented". */
    if (((op >> 28) & 0x1u) != 0u) {
        return false;
    }
    /* A8.8.98 MCR, MCR2 (p. A8-476) and A8.8.107 MRC, MRC2 (p. A8-492) carry
       "t == 13 && (CurrentInstrSet() != InstrSet_ARM) then UNPREDICTABLE";
       A8.8.99 MCRR, MCRR2 (p. A8-478) and A8.8.108 MRRC, MRRC2 (p. A8-494)
       carry it for t and t2 alike, as do the extension-register transfers
       sharing those encodings: A8.8.314 VDUP (p. A8-886) and the A8.8.341
       (p. A8-940), A8.8.342 (p. A8-942), A8.8.343 (p. A8-944), A8.8.344
       (p. A8-946) and A8.8.345 (p. A8-948) VMOV forms. Rt is bits[15:12] and
       Rt2 bits[19:16]; on Table A6-30's LDC/STC and CDP rows bits[15:12] is
       CRd, not a core register. */
    if ((op1 & 0x3Eu) == 0x04u) {
        if (((op >> 12) & 0xFu) == 13u || ((op >> 16) & 0xFu) == 13u) {
            return false;
        }
    } else if ((op1 & 0x30u) == 0x20u && ((op >> 4) & 0x1u) != 0u &&
               ((op >> 12) & 0xFu) == 13u) {
        return false;
    }

    arm.word = op;
    return coproc_decoder_->Decode(insn, arm);
}

/* ARM DDI 0406C.c Table A6-11 (A6.3.2, p. A6-232): the key i:imm3:a selects
   the placement of abcdefgh. Keys 00xxx replicate the byte into one, two or
   four positions; every other key is the rotation applied to 1bcdefgh. */
uint32_t Thumb32Decoder::ThumbExpandImm(uint32_t key, uint32_t imm8) const {
    if ((key >> 3) == 0u) {
        switch ((key >> 1) & 0x3u) {
        case 0u:  return imm8;
        case 1u:  return (imm8 << 16) | imm8;
        case 2u:  return (imm8 << 24) | (imm8 << 8);
        default:  return (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8;
        }
    }
    const uint32_t value = 0x80u | (imm8 & 0x7Fu);
    return (value >> key) | (value << (32u - key));
}

/* ARM DDI 0406C.c Table A6-10 (A6.3.1, p. A6-231) and Table A6-22 (A6.3.11,
   p. A6-243) allocate op identically over the rows they share, keyed on Rn ==
   1111 for the 0010 and 0011 rows and on Rd:S == 11111 for the test aliases;
   both state "Other encodings in this space are UNDEFINED". The opcode values
   are the ARM data-processing numbering of Table A5-5 (p. A5-199). */
bool Thumb32Decoder::MapDataProcessingOpcode(uint32_t o, uint32_t rn,
                                             uint32_t rd, uint32_t s,
                                             uint32_t* opcode,
                                             bool* test) const {
    const bool wide_rd = rd == 0xFu && s != 0u;
    *test = false;
    switch (o) {
    case 0x0u: *test = wide_rd; *opcode = *test ? 8u : 0u; return true;
    case 0x1u: *opcode = 14u; return true;
    case 0x2u: *opcode = rn == 0xFu ? 13u : 12u; return true;
    case 0x3u:
        *opcode = rn == 0xFu ? 15u : static_cast<uint32_t>(kDpOrn);
        return true;
    case 0x4u: *test = wide_rd; *opcode = *test ? 9u : 1u; return true;
    case 0x8u: *test = wide_rd; *opcode = *test ? 11u : 4u; return true;
    case 0xAu: *opcode = 5u; return true;
    case 0xBu: *opcode = 6u; return true;
    case 0xDu: *test = wide_rd; *opcode = *test ? 10u : 2u; return true;
    case 0xEu: *opcode = 3u; return true;
    default:   return false;
    }
}

/* ARM DDI 0406C.c: rows 1000 and 1101 of Tables A6-10 and A6-22 carry
   "if Rn == '1101' then SEE ADD (SP plus immediate) / ADD (SP plus register) /
   SUB (SP minus immediate) / SUB (SP minus register)" - A8.8.4 (p. A8-306),
   A8.8.6 (p. A8-310), A8.8.221 (p. A8-708), A8.8.223 (p. A8-712). Those four
   redirect targets - A8.8.9 (p. A8-316), A8.8.10 (p. A8-318), A8.8.225
   (p. A8-716), A8.8.226 (p. A8-718) - permit d == 13, which every other row
   forbids: "d == 13 || (d == 15 && S == '0')" on A8.8.13 AND (immediate)
   (p. A8-324) and its siblings. Rn == 15 is UNPREDICTABLE on the 1000 and 1101
   rows and "n IN {13,15}" elsewhere, except the 0010 and 0011 rows, which
   carry "d IN {13,15} || n == 13" - A8.8.122 ORR (immediate) (p. A8-516) and
   A8.8.120 ORN (immediate) (p. A8-512) - because their Rn == 1111 entries are
   the separate MOV and MVN rows, whose own "d IN {13,15}" is on A8.8.102
   (p. A8-484) and A8.8.115 (p. A8-504). */
bool Thumb32Decoder::DataProcRegistersValid(uint32_t o, uint32_t rn,
                                            uint32_t rd, bool test,
                                            bool* sp_form) const {
    *sp_form = (o == 0x8u || o == 0xDu) && rn == 13u;
    if (!test && (rd == 0xFu || (rd == 13u && !*sp_form))) {
        return false;
    }
    if (*sp_form) {
        return true;
    }
    if ((o == 0x2u || o == 0x3u) && rn == 0xFu) {
        return true;
    }
    return rn != 0xFu && rn != 13u;
}

/* ARM DDI 0406C.c Table A6-10 and the encoding diagram above it (A6.3.1,
   p. A6-231): i = bit[26], op = bits[24:21], S = bit[20], Rn = bits[19:16],
   imm3 = bits[14:12], Rd = bits[11:8], imm8 = bits[7:0]; "Other encodings in
   this space are UNDEFINED". */
bool Thumb32Decoder::DecodeDataProcessingModifiedImmediate(DecodedInsn* insn,
                                                           uint32_t op) {
    const uint32_t o    = (op >> 21) & 0xFu;
    const uint32_t s    = (op >> 20) & 0x1u;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t rd   = (op >>  8) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;
    const uint32_t key  = (((op >> 26) & 0x1u) << 4) |
                          (((op >> 12) & 0x7u) << 1) | ((imm8 >> 7) & 0x1u);
    uint32_t opcode = 0u;
    bool     test   = false;
    if (!MapDataProcessingOpcode(o, rn, rd, s, &opcode, &test)) {
        return false;
    }
    bool sp_form = false;
    if (!DataProcRegistersValid(o, rn, rd, test, &sp_form)) {
        return false;
    }

    insn->op1       = opcode;
    insn->s         = s;
    insn->rn        = rn;
    insn->rd        = rd;
    insn->immediate = ThumbExpandImm(key, imm8);
    /* A6.3.2 "Carry out" (p. A6-232): "A logical instruction with i:imm3:a ==
       '00xxx' does not affect the Carry flag. Otherwise, a logical flag-setting
       instruction sets the Carry flag to the value of bit[31] of the modified
       immediate constant." */
    insn->rs        = key >= 8u ? key : 0u;
    insn->place_fn  = &PlaceDataProcessing;
    return true;
}

bool Thumb32Decoder::DecodeDataProcessingPlainBinaryImmediate(DecodedInsn* insn,
                                                              uint32_t op) {
    Unimplemented("data-processing (plain binary immediate) (A6-234)", insn,
                  op);
}

bool Thumb32Decoder::DecodeStoreSingleDataItem(DecodedInsn* insn, uint32_t op) {
    Unimplemented("store single data item (A6-242)", insn, op);
}

bool Thumb32Decoder::DecodeLoadByteMemoryHints(DecodedInsn* insn, uint32_t op) {
    Unimplemented("load byte, memory hints (A6-241)", insn, op);
}

bool Thumb32Decoder::DecodeLoadHalfwordMemoryHints(DecodedInsn* insn,
                                                   uint32_t op) {
    Unimplemented("load halfword, memory hints (A6-240)", insn, op);
}

bool Thumb32Decoder::DecodeLoadWord(DecodedInsn* insn, uint32_t op) {
    Unimplemented("load word (A6-239)", insn, op);
}

bool Thumb32Decoder::DecodeSimdElementOrStructure(DecodedInsn* insn,
                                                  uint32_t op) {
    Unimplemented("Advanced SIMD element or structure load/store (A7-275)",
                  insn, op);
}

bool Thumb32Decoder::DecodeDataProcessingRegister(DecodedInsn* insn,
                                                  uint32_t op) {
    Unimplemented("data-processing (register) (A6-245)", insn, op);
}

bool Thumb32Decoder::DecodeMultiplyAbsoluteDifference(DecodedInsn* insn,
                                                      uint32_t op) {
    Unimplemented("multiply, multiply accumulate, absolute difference "
                  "(A6-249)", insn, op);
}

bool Thumb32Decoder::DecodeLongMultiplyDivide(DecodedInsn* insn, uint32_t op) {
    Unimplemented("long multiply, long multiply accumulate, divide (A6-250)",
                  insn, op);
}

/* ARM DDI 0406C.c Table A6-9 (A6.3, p. A6-230), keyed on op1 = bits[12:11] of
   the first halfword, op2 = bits[10:4] of the first halfword and op = bit[15]
   of the second halfword. */
bool Thumb32Decoder::DecodeThumb32(DecodedInsn* insn, uint32_t op) {
    insn->cond = 14u;

    const uint32_t op1 = (op >> 27) & 0x3u;
    const uint32_t op2 = (op >> 20) & 0x7Fu;
    const uint32_t o   = (op >> 15) & 0x1u;

    switch (op1) {
    case 0x1u:
        if ((op2 & 0x40u) != 0u) return DecodeCoprocessorSimdFp(insn, op);
        if ((op2 & 0x20u) != 0u) {
            return DecodeDataProcessingShiftedRegister(insn, op);
        }
        if ((op2 & 0x04u) != 0u) {
            return DecodeLoadStoreDualExclusiveTableBranch(insn, op);
        }
        return DecodeLoadStoreMultiple(insn, op);
    case 0x2u:
        if (o != 0u) return DecodeBranchesMiscControl(insn, op);
        if ((op2 & 0x20u) != 0u) {
            return DecodeDataProcessingPlainBinaryImmediate(insn, op);
        }
        return DecodeDataProcessingModifiedImmediate(insn, op);
    case 0x3u:
        if ((op2 & 0x40u) != 0u) return DecodeCoprocessorSimdFp(insn, op);
        if ((op2 & 0x20u) != 0u) {
            if ((op2 & 0x10u) == 0u) {
                return DecodeDataProcessingRegister(insn, op);
            }
            return (op2 & 0x08u) != 0u
                       ? DecodeLongMultiplyDivide(insn, op)
                       : DecodeMultiplyAbsoluteDifference(insn, op);
        }
        if ((op2 & 0x01u) == 0u) {
            return (op2 & 0x10u) != 0u
                       ? DecodeSimdElementOrStructure(insn, op)
                       : DecodeStoreSingleDataItem(insn, op);
        }
        switch (op2 & 0x07u) {
        case 0x1u: return DecodeLoadByteMemoryHints(insn, op);
        case 0x3u: return DecodeLoadHalfwordMemoryHints(insn, op);
        case 0x5u: return DecodeLoadWord(insn, op);
        default:   return false;
        }
    default:
        Unimplemented("op1 == 0b00 is a 16-bit encoding", insn, op);
    }
}
