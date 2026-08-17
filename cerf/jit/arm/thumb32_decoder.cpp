#include "thumb32_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_decoder.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(Thumb32Decoder);

bool Thumb32Decoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32Decoder::OnReady() {
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
    arm_decoder_      = &emu_.Get<ArmDecoder>();
    has_thumb2_       = processor_config_->HasThumb2();
}

void Thumb32Decoder::Unimplemented(const char* what, const DecodedInsn* insn,
                                   uint32_t op) {
    emu_.Get<Fatal>().Die("Thumb32Decoder: %s not implemented, op=0x%08X "
                          "at guest PC 0x%08X\n",
                          what, op, insn->guest_address);
}

bool Thumb32Decoder::DecodeLoadStoreMultiple(DecodedInsn* insn, uint32_t op) {
    /* ARM DDI 0406C.c Table A6-17 (p. A6-237): Thumb-2 LDM/STM T2,
       including PUSH.W/POP.W aliases, retains the ARM P/U/W/L, Rn and
       register-list field positions. */
    if ((op & 0xFE000000u) == 0xE8000000u) {
        return arm_decoder_->DecodeArm(insn, ArmOpcode{op});
    }
    Unimplemented("load/store multiple (A6-237)", insn, op);
}

bool Thumb32Decoder::DecodeBranchesMiscControl(DecodedInsn* insn, uint32_t op) {
    /* ARM DDI 0406C.c A8.8.25 BLX immediate T2 (p. A8-350). */
    if ((op & 0xF800D000u) == 0xF000C000u) {
        const uint32_t s  = (op >> 26) & 1u;
        const uint32_t j1 = (op >> 13) & 1u;
        const uint32_t j2 = (op >> 11) & 1u;
        const uint32_t i1 = (~(j1 ^ s)) & 1u;
        const uint32_t i2 = (~(j2 ^ s)) & 1u;
        uint32_t displacement = (s << 24) | (i1 << 23) | (i2 << 22) |
                                (((op >> 16) & 0x3FFu) << 12) |
                                ((op & 0x7FFu) << 1);
        if (s != 0u) displacement |= 0xFE000000u;
        insn->l = 1u;
        insn->n = 1u;  /* BLX immediate marker for PlaceBranch. */
        insn->offset = static_cast<int32_t>(displacement);
        insn->r15_modified = true;
        insn->place_fn = &PlaceBranch;
        return true;
    }
    /* ARM DDI 0406C.c A8.8.32 CLREX T1 (p. A8-360). */
    if (op == 0xF3BF8F2Fu) {
        insn->place_fn = &PlaceClrex;
        return true;
    }
    /* ARM DDI 0406C.c B9.3.2 CPS T2 (p. B9-1979). */
    if ((op & 0xFFFFF100u) == 0xF3AF8100u) {
        insn->op1       = (op >> 9) & 3u;
        insn->rn        = (op >> 5) & 7u;
        insn->immediate = op & 0x1Fu;
        insn->context_sync = (op & 0x1Fu) != 0u;
        insn->place_fn  = &PlaceCpsMode;
        return true;
    }
    /* ARM DDI 0406C.c B9.3.6 ERET T1 (p. B9-1989). MOVS PC,LR uses the
       common 6.8 exception-return path. */
    if (op == 0xF3DE8F00u) {
        return arm_decoder_->DecodeArm(insn, ArmOpcode{0xE1B0F00Eu});
    }
    /* ARM DDI 0406C.c A8.8.18 B T3/T4 (pp. A8-334/A8-335) and A8.8.25
       BL T1 (p. A8-348): reconstruct the signed Thumb-2 displacement from
       S/J1/J2 and I1/I2. PlaceBranch adds it to the Thumb PC read value. */
    if ((op & 0xF800D000u) == 0xF0008000u) {
        const uint32_t cond = (op >> 22) & 0xFu;
        if (cond < 0xEu) {
            const uint32_t s  = (op >> 26) & 0x1u;
            const uint32_t j1 = (op >> 13) & 0x1u;
            const uint32_t j2 = (op >> 11) & 0x1u;
            uint32_t displacement = (s << 20) | (j2 << 19) | (j1 << 18) |
                                    (((op >> 16) & 0x3Fu) << 12) |
                                    ((op & 0x7FFu) << 1);
            if (s != 0u) displacement |= 0xFFE00000u;
            insn->cond         = cond;
            insn->offset       = static_cast<int32_t>(displacement);
            insn->r15_modified = true;
            insn->place_fn     = &PlaceBranch;
            return true;
        }
    }
    if ((op & 0xF800D000u) == 0xF0009000u ||
        (op & 0xF800D000u) == 0xF000D000u) {
        const uint32_t s  = (op >> 26) & 0x1u;
        const uint32_t j1 = (op >> 13) & 0x1u;
        const uint32_t j2 = (op >> 11) & 0x1u;
        const uint32_t i1 = (~(j1 ^ s)) & 0x1u;
        const uint32_t i2 = (~(j2 ^ s)) & 0x1u;
        uint32_t displacement = (s << 24) | (i1 << 23) | (i2 << 22) |
                                (((op >> 16) & 0x3FFu) << 12) |
                                ((op & 0x7FFu) << 1);
        if (s != 0u) displacement |= 0xFE000000u;
        insn->l            = (op & 0x4000u) != 0u ? 1u : 0u;
        insn->offset       = static_cast<int32_t>(displacement);
        insn->r15_modified = true;
        insn->place_fn     = &PlaceBranch;
        return true;
    }
    /* ARM DDI 0406C.c A8.8.44 / A8.8.43 / A8.8.53, encoding T1:
       F3BF 8F4x/8F5x/8F6x are DSB/DMB/ISB. The same 6.8 ARM decoder models
       DSB/DMB as NOPs on x86 and marks ISB as context synchronization. */
    if (processor_config_->HasBarrierInsn() &&
        (op & 0xFFFFFF00u) == 0xF3BF8F00u) {
        const uint32_t barrier_op = (op >> 4) & 0xFu;
        if (barrier_op >= 0x4u && barrier_op <= 0x6u) {
            insn->context_sync = barrier_op == 0x6u;
            insn->place_fn     = &PlaceNop;
            return true;
        }
    }
    /* ARM DDI 0406C.c B9.3.8 MRS, encoding T1 (p. B9-1991):
       11110 0111110 1111 10 0 0 Rd 00000000 reads APSR/CPSR. */
    if ((op & 0xFFFFF0FFu) == 0xF3EF8000u) {
        insn->s        = 0u;
        insn->n        = 0u;
        insn->rd       = (op >> 8) & 0xFu;
        insn->place_fn = &PlaceMRSorMSR;
        return true;
    }
    /* ARM DDI 0406C.c B9.3.8 MRS SPSR, encoding T1. */
    if ((op & 0xFFFFF0FFu) == 0xF3FF8000u) {
        insn->s        = 0u;
        insn->n        = 1u;
        insn->rd       = (op >> 8) & 0xFu;
        insn->place_fn = &PlaceMRSorMSR;
        return true;
    }
    /* ARM DDI 0406C.c B9.3.12 MSR (register), encoding T1
       (p. B9-1998): R = bit[20], mask = bits[11:8], Rn = bits[19:16]. */
    if ((op & 0xFFE0F0FFu) == 0xF3808000u) {
        insn->s        = 1u;
        insn->n        = (op >> 20) & 0x1u;
        insn->crn      = (op >> 8) & 0xFu;
        insn->rm       = (op >> 16) & 0xFu;
        insn->place_fn = &PlaceMRSorMSR;
        return true;
    }
    Unimplemented("branches and miscellaneous control (A6-235)", insn, op);
}

bool Thumb32Decoder::DecodeCoprocessorSimdFp(DecodedInsn* insn, uint32_t op) {
    /* ARM DDI 0406C.c Table A6-30 (p. A6-251): Thumb coprocessor
       instructions use the ARM coprocessor field layout. The leading 1110
       is the unconditional ARM condition, so the 6.8 ARM decoder can apply
       the common CP10/CP11/CP15 capability and permission checks directly. */
    if ((op >> 28) == 0xEu) {
        return arm_decoder_->DecodeArm(insn, ArmOpcode{op});
    }
    Unimplemented("coprocessor, Advanced SIMD, floating-point (A6-251)", insn,
                  op);
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
    const bool     wide_rd = rd == 0xFu && s != 0u;

    uint32_t opcode;
    bool     test = false;
    switch (o) {
    case 0x0u: test = wide_rd; opcode = test ? 8u : 0u; break;
    case 0x1u: opcode = 14u; break;
    case 0x2u: opcode = rn == 0xFu ? 13u : 12u; break;
    case 0x3u: opcode = rn == 0xFu ? 15u : static_cast<uint32_t>(kDpOrn); break;
    case 0x4u: test = wide_rd; opcode = test ? 9u : 1u; break;
    case 0x8u: test = wide_rd; opcode = test ? 11u : 4u; break;
    case 0xAu: opcode = 5u; break;
    case 0xBu: opcode = 6u; break;
    case 0xDu: test = wide_rd; opcode = test ? 10u : 2u; break;
    case 0xEu: opcode = 3u; break;
    default:   return false;
    }
    /* d == 15 is UNPREDICTABLE across this class: A8.8.13 AND (immediate)
       (p. A8-324), A8.8.4 ADD (immediate, Thumb) T3 (p. A8-306) and A8.8.221
       SUB (immediate, Thumb) T3 (p. A8-708) when S == 0; A8.8.120 ORN
       (immediate) (p. A8-512), A8.8.102 MOV (immediate) T2 (p. A8-484) and
       A8.8.115 MVN (immediate) T1 (p. A8-504) outright. */
    if (!test && rd == 0xFu) return false;

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

bool Thumb32Decoder::DecodeSimdElementOrStructure(DecodedInsn* insn,
                                                  uint32_t op) {
    Unimplemented("Advanced SIMD element or structure load/store (A7-275)",
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
