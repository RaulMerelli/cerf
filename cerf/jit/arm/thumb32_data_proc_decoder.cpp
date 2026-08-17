#include "thumb32_data_proc_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb32_fatal.h"

REGISTER_SERVICE(Thumb32DataProcDecoder);

bool Thumb32DataProcDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32DataProcDecoder::OnReady() {
    fatal_ = &emu_.Get<Thumb32Fatal>();
}

/* ARM DDI 0406C.c "Move register and immediate shifts" and Table A6-23
   (A6.3.11, p. A6-244): type = bits[5:4] with imm3:imm2 select A8.8.103 MOV
   (register, Thumb) (p. A8-486), A8.8.94 LSL (immediate) (p. A8-468), A8.8.96
   LSR (immediate) (p. A8-472), A8.8.16 ASR (immediate) (p. A8-330), A8.8.151
   RRX (p. A8-572) and A8.8.149 ROR (immediate) (p. A8-568). The five shift
   rows carry "if d IN {13,15} || m IN {13,15} then UNPREDICTABLE"; MOV
   encoding T3 instead carries "if setflags && (d IN {13,15} || m IN {13,15})"
   and "if !setflags && (d == 15 || m == 15 || (d == 13 && m == 13))". */
bool Thumb32DataProcDecoder::DecodeMoveRegisterImmediateShifts(
    DecodedInsn* insn, uint32_t op) {
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
bool Thumb32DataProcDecoder::DecodeDataProcessingShiftedRegister(
    DecodedInsn* insn, uint32_t op) {
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

/* ARM DDI 0406C.c Table A6-11 (A6.3.2, p. A6-232): the key i:imm3:a selects
   the placement of abcdefgh. Keys 00xxx replicate the byte into one, two or
   four positions; every other key is the rotation applied to 1bcdefgh. */
uint32_t Thumb32DataProcDecoder::ThumbExpandImm(uint32_t key,
                                                uint32_t imm8) const {
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
bool Thumb32DataProcDecoder::MapDataProcessingOpcode(uint32_t o, uint32_t rn,
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
bool Thumb32DataProcDecoder::DataProcRegistersValid(uint32_t o, uint32_t rn,
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
bool Thumb32DataProcDecoder::DecodeDataProcessingModifiedImmediate(
    DecodedInsn* insn, uint32_t op) {
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

bool Thumb32DataProcDecoder::DecodeDataProcessingPlainBinaryImmediate(
    DecodedInsn* insn, uint32_t op) {
    fatal_->Unimplemented("data-processing (plain binary immediate) (A6-234)",
                          insn, op);
}

/* ARM DDI 0406C.c A6.3.12 and Table A6-24 (p. A6-245): op1 = bits[23:20],
   Rn = bits[19:16], Rd = bits[11:8], op2 = bits[7:4], Rm = bits[3:0]; "If, in
   the second halfword of the instruction, bits[15:12] != 0b1111, the
   instruction is UNDEFINED", and "Other encodings in this space are
   UNDEFINED". The op1 = 000x / 001x / 010x / 011x rows with op2 = 0000 are
   A8.8.95 LSL (register) (p. A8-470), A8.8.97 LSR (register) (p. A8-474),
   A8.8.17 ASR (register) (p. A8-332) and A8.8.150 ROR (register)
   (p. A8-570), whose encoding T2 is "d = UInt(Rd); n = UInt(Rn);
   m = UInt(Rm); setflags = (S == '1')" with
   "if d IN {13,15} || n IN {13,15} || m IN {13,15} then UNPREDICTABLE" - so
   the low bit of op1 is S and bits[3:1] are the SRType. */
bool Thumb32DataProcDecoder::DecodeDataProcessingRegister(DecodedInsn* insn,
                                                          uint32_t op) {
    const uint32_t op1 = (op >> 20) & 0xFu;
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rd  = (op >>  8) & 0xFu;
    const uint32_t op2 = (op >>  4) & 0xFu;
    const uint32_t rm  =  op        & 0xFu;

    if (((op >> 12) & 0xFu) != 0xFu) {
        return false;
    }

    if ((op1 & 0x8u) == 0u && op2 == 0x0u) {
        if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
            rm == 13u || rm == 0xFu) {
            return false;
        }
        insn->op1      = 13u;
        insn->s        = op1 & 0x1u;
        insn->rd       = rd;
        insn->rm       = rn;
        insn->rs       = rm;
        insn->n        = (op1 >> 1) & 0x3u;
        insn->place_fn = &PlaceDataProcessingShiftedReg;
        return true;
    }

    /* A8.8.235 SXTH (p. A8-734), A8.8.276 UXTH (p. A8-816), A8.8.233 SXTB
       (p. A8-730) and A8.8.274 UXTB (p. A8-812), encoding T2: "d = UInt(Rd);
       m = UInt(Rm); rotation = UInt(rotate:'000')" with "if d IN {13,15} ||
       m IN {13,15} then UNPREDICTABLE"; rotate is bits[5:4] and bits[6] is
       (0). */
    if (op1 <= 0x5u && (op2 & 0x8u) != 0u) {
        if ((op2 & 0x4u) != 0u) {
            return false;
        }
        /* Table A6-24 (p. A6-245) rows 0010 and 0011: Rn == 1111 is "Signed /
           Unsigned Extend Byte 16-bit", A8.8.234 SXTB16 (p. A8-732) /
           A8.8.275 UXTB16 (p. A8-814); Rn != 1111 is "Signed / Unsigned
           Extend and Add Byte 16-bit", A8.8.231 SXTAB16 (p. A8-726) /
           A8.8.272 UXTAB16 (p. A8-808). */
        if (op1 == 0x2u || op1 == 0x3u) {
            fatal_->Unimplemented(rn == 0xFu
                                      ? "signed / unsigned extend byte 16 "
                                        "(A6-245)"
                                      : "signed / unsigned extend and add "
                                        "byte 16 (A6-245)",
                                  insn, op);
        }
        /* Table A6-24 (p. A6-245) rows 0000 / 0001 / 0100 / 0101 with
           Rn != 1111: A8.8.232 SXTAH (p. A8-728), A8.8.273 UXTAH (p. A8-810),
           A8.8.230 SXTAB (p. A8-724), A8.8.271 UXTAB (p. A8-806). */
        if (rn != 0xFu) {
            fatal_->Unimplemented("signed / unsigned extend and add (A6-245)",
                                  insn, op);
        }
        if (rd == 13u || rd == 0xFu || rm == 13u || rm == 0xFu) {
            return false;
        }
        insn->rd  = rd;
        insn->rm  = rm;
        insn->op1 = (op >> 4) & 0x3u;
        switch (op1) {
        case 0x0u: insn->place_fn = &PlaceSxth; break;
        case 0x1u: insn->place_fn = &PlaceUxth; break;
        case 0x4u: insn->place_fn = &PlaceSxtb; break;
        default:   insn->place_fn = &PlaceUxtb; break;
        }
        return true;
    }
    if ((op1 & 0x8u) != 0u && (op2 & 0xCu) == 0x0u) {
        fatal_->Unimplemented(
            "parallel addition and subtraction, signed (A6-246)", insn, op);
    }
    if ((op1 & 0x8u) != 0u && (op2 & 0xCu) == 0x4u) {
        fatal_->Unimplemented(
            "parallel addition and subtraction, unsigned (A6-247)", insn, op);
    }
    if ((op1 & 0xCu) == 0x8u && (op2 & 0xCu) == 0x8u) {
        fatal_->Unimplemented("miscellaneous operations (A6-248)", insn, op);
    }
    return false;
}
