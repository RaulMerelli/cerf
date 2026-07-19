#include "mips16_decoder.h"

#include "mips_opcode.h"
#include "mips_place_fns.h"

Mips16DecodeKind Mips16Decoder::Decode(uint16_t hw0, uint16_t hw1, uint32_t pc,
                                       uint32_t base_pc, MipsDecodedInsn* d,
                                       uint32_t* synth) const {
    if ((hw0 >> 11) == 0x1Eu) {
        return DecodeExtended(hw0, hw1, pc, base_pc, d, synth);
    }
    return DecodeBase(hw0, hw1, pc, base_pc, d, synth);
}

/* Non-extended forms: field positions per U15509EJ2V0UM 3.6 p59-61;
   immediates/shifts per the per-instruction pages (Tables 3-14..3-20 p71-83). */
Mips16DecodeKind Mips16Decoder::DecodeBase(uint16_t hw0, uint16_t hw1,
                                           uint32_t pc, uint32_t base_pc,
                                           MipsDecodedInsn* d,
                                           uint32_t* synth) const {
    const uint32_t op = hw0 >> 11;
    const uint32_t rx = Xlat(hw0 >> 8);
    const uint32_t ry = Xlat(hw0 >> 5);

    switch (op) {
        case 0x00:  /* addiusp: ADDIU rx, sp, imm8<<2 zero-extended (p74) */
            *synth = I(MipsOp::kADDIU, 29, rx, (hw0 & 0xFFu) << 2);
            return Mips16DecodeKind::kSynth;

        case 0x01:  /* addiupc: rx = (BasePC & ~3) + imm8<<2 (p74) */
            d->rt       = rx;
            d->target   = (base_pc & ~3u) + ((hw0 & 0xFFu) << 2);
            d->place_fn = &PlaceMips16Addiupc;
            return Mips16DecodeKind::kDirect;

        case 0x02:  /* b: 11-bit signed <<1 off the next insn, no delay slot (p83) */
            d->target     = pc + 2u + (Sext(hw0 & 0x7FFu, 11) << 1);
            d->place_fn   = &PlaceMips16B;
            d->ends_block = 1;
            return Mips16DecodeKind::kDirect;

        case 0x03: {  /* jal/jalx: imm26 scrambled (p61); base = delay-slot addr (p82) */
            const uint32_t imm26 =
                ((hw0 & 0x1Fu) << 21) | (((hw0 >> 5) & 0x1Fu) << 16) | hw1;
            d->target    = ((pc + 4u) & 0xF0000000u) | (imm26 << 2);
            d->place_fn  = (hw0 & 0x400u) ? &PlaceMips16Jalx : &PlaceMips16Jal;
            d->is_branch = 1;
            return Mips16DecodeKind::kDirect;
        }

        case 0x04:  /* beqz rx: int8<<1, no delay slot (p83) */
        case 0x05:  /* bnez rx (p83) */
            d->rs         = rx;
            d->target     = pc + 2u + (Sext(hw0 & 0xFFu, 8) << 1);
            d->place_fn   = (op == 0x04u) ? &PlaceMips16Beqz : &PlaceMips16Bnez;
            d->ends_block = 1;
            return Mips16DecodeKind::kDirect;

        case 0x06: {  /* shift: shamt 0 means 8 (p60 Note); f=01 dsll (Table 3-9 p65) */
            uint32_t sa = (hw0 >> 2) & 7u;
            if (sa == 0) sa = 8;
            switch (hw0 & 3u) {
                case 0: *synth = R(0, ry, rx, sa, MipsSpecial::kSLL); break;
                case 1:
                    if (!has_64bit_) return Mips16DecodeKind::kReserved;
                    *synth = R(0, ry, rx, sa, MipsSpecial::kDSLL);
                    break;
                case 2: *synth = R(0, ry, rx, sa, MipsSpecial::kSRL); break;
                case 3: *synth = R(0, ry, rx, sa, MipsSpecial::kSRA); break;
            }
            return Mips16DecodeKind::kSynth;
        }

        case 0x07:  /* ld ry, off5<<3(rx) (p72) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = I(MipsOp::kLD, rx, ry, (hw0 & 0x1Fu) << 3);
            return Mips16DecodeKind::kSynth;

        case 0x08: {  /* rri-a: imm4 signed; F=1 daddiu (Table 3-8 p65; p74/75) */
            const uint32_t imm = Sext(hw0 & 0xFu, 4);
            if ((hw0 >> 4) & 1u) {
                if (!has_64bit_) return Mips16DecodeKind::kReserved;
                *synth = I(MipsOp::kDADDIU, rx, ry, imm);
            } else {
                *synth = I(MipsOp::kADDIU, rx, ry, imm);
            }
            return Mips16DecodeKind::kSynth;
        }

        case 0x09:  /* addiu8: int8 (p74) */
            *synth = I(MipsOp::kADDIU, rx, rx, Sext(hw0 & 0xFFu, 8));
            return Mips16DecodeKind::kSynth;

        case 0x0A:  /* slti: imm8 zero-extended, result -> T($24) (p75) */
            *synth = I(MipsOp::kSLTI, rx, 24, hw0 & 0xFFu);
            return Mips16DecodeKind::kSynth;

        case 0x0B:  /* sltiu: imm8 zero-extended (p75) */
            *synth = I(MipsOp::kSLTIU, rx, 24, hw0 & 0xFFu);
            return Mips16DecodeKind::kSynth;

        case 0x0C:  /* i8 minor (Table 3-10 p65) */
            switch ((hw0 >> 8) & 7u) {
                case 0:  /* bteqz: T($24)==0, int8<<1, no delay slot (p83) */
                case 1:  /* btnez (p83) */
                    d->rs         = 24;
                    d->target     = pc + 2u + (Sext(hw0 & 0xFFu, 8) << 1);
                    d->place_fn   = (((hw0 >> 8) & 7u) == 0u) ? &PlaceMips16Beqz
                                                              : &PlaceMips16Bnez;
                    d->ends_block = 1;
                    return Mips16DecodeKind::kDirect;
                case 2:  /* swrasp: SW ra, off8<<2(sp) (p73) */
                    *synth = I(MipsOp::kSW, 29, 31, (hw0 & 0xFFu) << 2);
                    return Mips16DecodeKind::kSynth;
                case 3:  /* adjsp: ADDIU sp, int8<<3 (p74) */
                    *synth = I(MipsOp::kADDIU, 29, 29, Sext(hw0 & 0xFFu, 8) << 3);
                    return Mips16DecodeKind::kSynth;
                case 5: {  /* mov32r: MOVE r32, rz - full-register copy (p77);
                             r32[2:0,4:3] scrambled (p61) */
                    const uint32_t r32 =
                        (((hw0 >> 3) & 3u) << 3) | ((hw0 >> 5) & 7u);
                    *synth = R(Xlat(hw0), 0, r32, 0, MipsSpecial::kOR);
                    return Mips16DecodeKind::kSynth;
                }
                case 7:  /* movr32: MOVE ry, r32 - full-register copy (p60; p77) */
                    *synth = R(hw0 & 0x1Fu, 0, ry, 0, MipsSpecial::kOR);
                    return Mips16DecodeKind::kSynth;
                default:  /* 100 svrs / 110: '*' reserved (Table 3-10 p65) */
                    return Mips16DecodeKind::kReserved;
            }

        case 0x0D:  /* li: imm8 zero-extended (p74) */
            *synth = I(MipsOp::kORI, 0, rx, hw0 & 0xFFu);
            return Mips16DecodeKind::kSynth;

        case 0x0E:  /* cmpi: XOR into T($24), imm8 zero-extended (p75) */
            *synth = I(MipsOp::kXORI, rx, 24, hw0 & 0xFFu);
            return Mips16DecodeKind::kSynth;

        case 0x0F:  /* sd ry, off5<<3(rx) (p73) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = I(MipsOp::kSD, rx, ry, (hw0 & 0x1Fu) << 3);
            return Mips16DecodeKind::kSynth;

        case 0x10:  /* lb ry, off5(rx) (p71) */
            *synth = I(MipsOp::kLB, rx, ry, hw0 & 0x1Fu);
            return Mips16DecodeKind::kSynth;
        case 0x11:  /* lh: off5<<1 (p71) */
            *synth = I(MipsOp::kLH, rx, ry, (hw0 & 0x1Fu) << 1);
            return Mips16DecodeKind::kSynth;
        case 0x12:  /* lwsp: LW rx, off8<<2(sp) (p71) */
            *synth = I(MipsOp::kLW, 29, rx, (hw0 & 0xFFu) << 2);
            return Mips16DecodeKind::kSynth;
        case 0x13:  /* lw ry, off5<<2(rx) (p71) */
            *synth = I(MipsOp::kLW, rx, ry, (hw0 & 0x1Fu) << 2);
            return Mips16DecodeKind::kSynth;
        case 0x14:  /* lbu (p71) */
            *synth = I(MipsOp::kLBU, rx, ry, hw0 & 0x1Fu);
            return Mips16DecodeKind::kSynth;
        case 0x15:  /* lhu: off5<<1 (p71) */
            *synth = I(MipsOp::kLHU, rx, ry, (hw0 & 0x1Fu) << 1);
            return Mips16DecodeKind::kSynth;

        case 0x16:  /* lwpc: LW rx, off8<<2(pc), BasePC low 2 bits cleared (p71) */
            d->rt       = rx;
            d->target   = (base_pc & ~3u) + ((hw0 & 0xFFu) << 2);
            d->place_fn = &PlaceMips16Lwpc;
            return Mips16DecodeKind::kDirect;

        case 0x17:  /* lwu: off5<<2 (p72) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = I(MipsOp::kLWU, rx, ry, (hw0 & 0x1Fu) << 2);
            return Mips16DecodeKind::kSynth;

        case 0x18:  /* sb (p73) */
            *synth = I(MipsOp::kSB, rx, ry, hw0 & 0x1Fu);
            return Mips16DecodeKind::kSynth;
        case 0x19:  /* sh: off5<<1 (p73) */
            *synth = I(MipsOp::kSH, rx, ry, (hw0 & 0x1Fu) << 1);
            return Mips16DecodeKind::kSynth;
        case 0x1A:  /* swsp: SW rx, off8<<2(sp) (p73) */
            *synth = I(MipsOp::kSW, 29, rx, (hw0 & 0xFFu) << 2);
            return Mips16DecodeKind::kSynth;
        case 0x1B:  /* sw ry, off5<<2(rx) (p73) */
            *synth = I(MipsOp::kSW, rx, ry, (hw0 & 0x1Fu) << 2);
            return Mips16DecodeKind::kSynth;

        case 0x1C: {  /* rrr (Table 3-7 p65; p76) */
            const uint32_t rz = Xlat(hw0 >> 2);
            switch (hw0 & 3u) {
                case 0:
                    if (!has_64bit_) return Mips16DecodeKind::kReserved;
                    *synth = R(rx, ry, rz, 0, MipsSpecial::kDADDU);
                    break;
                case 1: *synth = R(rx, ry, rz, 0, MipsSpecial::kADDU); break;
                case 2:
                    if (!has_64bit_) return Mips16DecodeKind::kReserved;
                    *synth = R(rx, ry, rz, 0, MipsSpecial::kDSUBU);
                    break;
                case 3: *synth = R(rx, ry, rz, 0, MipsSpecial::kSUBU); break;
            }
            return Mips16DecodeKind::kSynth;
        }

        case 0x1D:
            return DecodeRr(hw0, d, synth);

        case 0x1F:
            return DecodeI64((hw0 >> 8) & 7u, (hw0 >> 5) & 7u, hw0,
                             /*extended=*/false, 0, base_pc, d, synth);

        default:
            return Mips16DecodeKind::kReserved;
    }
}

/* RR minors, Table 3-6 p64: j(al)r variants per Note 1; dsrl/dsra shamt = the
   rx field, 0 means 8 (Note 2); funct 01001 = syscall (row 01 col 001; Table
   3-20 p83); '*' cells (0x01 sdbbp, 0x11 cnvt, 0x15) are reserved. */
Mips16DecodeKind Mips16Decoder::DecodeRr(uint16_t hw0, MipsDecodedInsn* d,
                                         uint32_t* synth) const {
    const uint32_t funct = hw0 & 0x1Fu;
    const uint32_t rxf   = (hw0 >> 8) & 7u;
    const uint32_t rx    = Xlat(rxf);
    const uint32_t ry    = Xlat(hw0 >> 5);

    switch (funct) {
        case 0x00:  /* j(al)r (Note 1 p64; p82) */
            switch ((hw0 >> 5) & 7u) {
                case 0:  /* jr rx */
                    *synth = R(rx, 0, 0, 0, MipsSpecial::kJR);
                    return Mips16DecodeKind::kSynth;
                case 1:  /* jr ra */
                    *synth = R(31, 0, 0, 0, MipsSpecial::kJR);
                    return Mips16DecodeKind::kSynth;
                case 2:  /* jalr ra, rx */
                    d->rs        = rx;
                    d->place_fn  = &PlaceMips16Jalr;
                    d->is_branch = 1;
                    return Mips16DecodeKind::kDirect;
                default:
                    return Mips16DecodeKind::kReserved;
            }
        case 0x02:  /* slt -> T($24) (p76) */
            *synth = R(rx, ry, 24, 0, MipsSpecial::kSLT);
            return Mips16DecodeKind::kSynth;
        case 0x03:  /* sltu (p76) */
            *synth = R(rx, ry, 24, 0, MipsSpecial::kSLTU);
            return Mips16DecodeKind::kSynth;
        case 0x04:  /* sllv ry, rx (p78) */
            *synth = R(rx, ry, ry, 0, MipsSpecial::kSLLV);
            return Mips16DecodeKind::kSynth;
        case 0x05:  /* break: 6-bit code at [10:5] (p83) */
            *synth = (((hw0 >> 5) & 0x3Fu) << 6) | MipsSpecial::kBREAK;
            return Mips16DecodeKind::kSynth;
        case 0x06:  /* srlv (p78) */
            *synth = R(rx, ry, ry, 0, MipsSpecial::kSRLV);
            return Mips16DecodeKind::kSynth;
        case 0x07:  /* srav (p78) */
            *synth = R(rx, ry, ry, 0, MipsSpecial::kSRAV);
            return Mips16DecodeKind::kSynth;
        case 0x08:  /* dsrl ry, shamt=rx field, 0->8 (Note 2 p64; p79) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(0, ry, ry, rxf == 0 ? 8u : rxf, MipsSpecial::kDSRL);
            return Mips16DecodeKind::kSynth;
        case 0x09:  /* syscall (Table 3-6 row 01 col 001; p83) */
            *synth = MipsSpecial::kSYSCALL;
            return Mips16DecodeKind::kSynth;
        case 0x0A:  /* cmp: XOR -> T($24) (p77) */
            *synth = R(rx, ry, 24, 0, MipsSpecial::kXOR);
            return Mips16DecodeKind::kSynth;
        case 0x0B:  /* neg rx, ry = SUBU rx, $0, ry (p77) */
            *synth = R(0, ry, rx, 0, MipsSpecial::kSUBU);
            return Mips16DecodeKind::kSynth;
        case 0x0C:  /* and rx, ry (p77) */
            *synth = R(rx, ry, rx, 0, MipsSpecial::kAND);
            return Mips16DecodeKind::kSynth;
        case 0x0D:  /* or (p77) */
            *synth = R(rx, ry, rx, 0, MipsSpecial::kOR);
            return Mips16DecodeKind::kSynth;
        case 0x0E:  /* xor (p77) */
            *synth = R(rx, ry, rx, 0, MipsSpecial::kXOR);
            return Mips16DecodeKind::kSynth;
        case 0x0F:  /* not rx, ry = NOR rx, ry, $0 (p77) */
            *synth = R(ry, 0, rx, 0, MipsSpecial::kNOR);
            return Mips16DecodeKind::kSynth;
        case 0x10:  /* mfhi rx (p80) */
            *synth = R(0, 0, rx, 0, MipsSpecial::kMFHI);
            return Mips16DecodeKind::kSynth;
        case 0x12:  /* mflo rx (p81) */
            *synth = R(0, 0, rx, 0, MipsSpecial::kMFLO);
            return Mips16DecodeKind::kSynth;
        case 0x13:  /* dsra ry, shamt=rx field, 0->8 (Note 2 p64; p79) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(0, ry, ry, rxf == 0 ? 8u : rxf, MipsSpecial::kDSRA);
            return Mips16DecodeKind::kSynth;
        case 0x14:  /* dsllv ry, rx (p79) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(rx, ry, ry, 0, MipsSpecial::kDSLLV);
            return Mips16DecodeKind::kSynth;
        case 0x16:  /* dsrlv (p79) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(rx, ry, ry, 0, MipsSpecial::kDSRLV);
            return Mips16DecodeKind::kSynth;
        case 0x17:  /* dsrav (p79) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(rx, ry, ry, 0, MipsSpecial::kDSRAV);
            return Mips16DecodeKind::kSynth;
        case 0x18:  /* mult rx, ry (p80) */
            *synth = R(rx, ry, 0, 0, MipsSpecial::kMULT);
            return Mips16DecodeKind::kSynth;
        case 0x19:  /* multu (p80) */
            *synth = R(rx, ry, 0, 0, MipsSpecial::kMULTU);
            return Mips16DecodeKind::kSynth;
        case 0x1A:  /* div (p80) */
            *synth = R(rx, ry, 0, 0, MipsSpecial::kDIV);
            return Mips16DecodeKind::kSynth;
        case 0x1B:  /* divu (p80) */
            *synth = R(rx, ry, 0, 0, MipsSpecial::kDIVU);
            return Mips16DecodeKind::kSynth;
        case 0x1C:  /* dmult (p81) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(rx, ry, 0, 0, MipsSpecial::kDMULT);
            return Mips16DecodeKind::kSynth;
        case 0x1D:  /* dmultu (p81) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(rx, ry, 0, 0, MipsSpecial::kDMULTU);
            return Mips16DecodeKind::kSynth;
        case 0x1E:  /* ddiv (p81) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(rx, ry, 0, 0, MipsSpecial::kDDIV);
            return Mips16DecodeKind::kSynth;
        case 0x1F:  /* ddivu (p81) */
            if (!has_64bit_) return Mips16DecodeKind::kReserved;
            *synth = R(rx, ry, 0, 0, MipsSpecial::kDDIVU);
            return Mips16DecodeKind::kSynth;
        default:
            return Mips16DecodeKind::kReserved;
    }
}
