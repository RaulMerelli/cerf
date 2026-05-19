#include "arm_decoder.h"

#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb_opcode.h"

namespace {

/* Thumb math-immediate opcode → ARM data-processing opcode map. */
constexpr uint8_t kMathImmediateToArm[4] = {
    13,  /* MOV */
    10,  /* CMP */
    4,   /* ADD */
    2,   /* SUB */
};

/* Thumb ALU opcode → ARM data-processing opcode map. 0xFF = no
   ARM equivalent (MUL is handled via ArithmeticExtension). */
constexpr uint8_t kAluOperationToArm[16] = {
    0,    /* AND  = AND  */
    1,    /* EOR  = EOR  */
    13,   /* LSL  = MOV  (shifter op) */
    13,   /* LSR  = MOV  */
    13,   /* ASR  = MOV  */
    5,    /* ADC  = ADC  */
    6,    /* SBC  = SBC  */
    13,   /* ROR  = MOV  */
    8,    /* TST  = TST  */
    3,    /* NEG  = RSB  */
    10,   /* CMP  = CMP  */
    11,   /* CMN  = CMN  */
    12,   /* ORR  = ORR  */
    0xFF, /* MUL  = ArithmeticExtension */
    14,   /* BIC  = BIC  */
    15,   /* MVN  = MVN  */
};

/* Thumb shifter index for ALU MOV-via-shift opcodes (LSL/LSR/ASR/ROR). */
constexpr uint8_t kAluOperationToArmShift[8] = {
    0xFF, 0xFF,
    0,    /* LSL */
    1,    /* LSR */
    2,    /* ASR */
    0xFF, 0xFF,
    3,    /* ROR */
};

constexpr uint8_t kHiOpsToArm[3] = {
    4,   /* ADD */
    10,  /* CMP */
    13,  /* MOV */
};

}  /* namespace */

void ArmDecoder::DecodeThumbMoveAddSub(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.data_processing.cond      = 0xEu;
    arm_op.data_processing.reserved1 = 0;
    arm_op.data_processing.s         = 1;

    switch (op.move_shifted_register.op2) {
    case 0: case 1: case 2:
        /* LSL / LSR / ASR Rd, Rs, #imm — synthesized as MOV Rd, Rs
           shifted by imm. */
        arm_op.data_processing.rd       = op.move_shifted_register.rd;
        arm_op.data_processing.i        = 0;
        arm_op.data_processing.operand2 = (op.move_shifted_register.offset5 << 7) |
                                          (op.move_shifted_register.op2     << 5) |
                                           op.move_shifted_register.rs;
        arm_op.data_processing.rn       = 0;
        arm_op.data_processing.opcode   = 13;  /* MOV */
        break;
    case 3:
        /* ADD/SUB Rd, Rs, Rn (or #imm3). */
        arm_op.data_processing.opcode   = (op.add_subtract.op == 0u) ? 4 : 2;
        arm_op.data_processing.rn       = op.add_subtract.rs;
        arm_op.data_processing.rd       = op.add_subtract.rd;
        arm_op.data_processing.i        = op.add_subtract.i;
        arm_op.data_processing.operand2 = op.add_subtract.rn_offset;
        break;
    default:
        break;
    }
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbMathImmediate(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.data_processing.cond      = 0xEu;
    arm_op.data_processing.reserved1 = 0;
    arm_op.data_processing.opcode    = kMathImmediateToArm[op.math_immediate.op];
    arm_op.data_processing.s         = 1;
    arm_op.data_processing.rn        = op.math_immediate.rd;  /* Op1 = Rd */
    arm_op.data_processing.rd        = op.math_immediate.rd;
    arm_op.data_processing.i         = 1;
    arm_op.data_processing.operand2  = op.math_immediate.offset8;  /* rot=0 */
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbALUOperation(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;

    if (op.alu_operation.op == 13u) {
        /* MUL Rd, Rs — synthesize ARM "MULS Rd, Rs, Rd" via
           ArithmeticExtension. */
        arm_op.arithmetic_extension.cond      = 0xEu;
        arm_op.arithmetic_extension.reserved2 = 0;
        arm_op.arithmetic_extension.op1       = 0;
        arm_op.arithmetic_extension.s         = 1;
        arm_op.arithmetic_extension.rd        = op.alu_operation.rd;
        arm_op.arithmetic_extension.rs        = op.alu_operation.rs;
        arm_op.arithmetic_extension.rn        = 0;
        arm_op.arithmetic_extension.reserved1 = 9;
        arm_op.arithmetic_extension.rm        = op.alu_operation.rd;
        DecodeArm(d, arm_op.word);
        return;
    }

    arm_op.data_processing.cond      = 0xEu;
    arm_op.data_processing.reserved1 = 0;
    arm_op.data_processing.opcode    = kAluOperationToArm[op.alu_operation.op];
    arm_op.data_processing.s         = 1;
    arm_op.data_processing.rd        = op.alu_operation.rd;

    if (arm_op.data_processing.opcode == 13u) {
        /* MOV-via-shift (LSL/LSR/ASR/ROR). Operand2 encodes
           Rd {LSL,LSR,ASR,ROR} Rs. */
        arm_op.data_processing.i        = 0;
        arm_op.data_processing.rn       = 0;
        arm_op.data_processing.operand2 = (op.alu_operation.rs << 8) |
            (kAluOperationToArmShift[op.alu_operation.op] << 5) |
            0x10u |
            op.alu_operation.rd;
    } else if (arm_op.data_processing.opcode == 3u) {
        /* NEG = RSB Rd, Rs, #0. */
        arm_op.data_processing.i        = 1;
        arm_op.data_processing.rn       = op.alu_operation.rs;
        arm_op.data_processing.operand2 = 0;
    } else {
        /* Standard 3-operand form: Op1=Rd, Op2=Rs (shift-by-0). */
        arm_op.data_processing.i        = 0;
        arm_op.data_processing.rn       = op.alu_operation.rd;
        arm_op.data_processing.operand2 = op.alu_operation.rs;
    }
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbHiOps(DecodedInsn* d, ThumbOpcode op) {
    if (op.hi_ops.op == 3u) {
        /* BX (branch-and-exchange) — Thumb-specific Place fn. */
        d->rs_hs        = op.hi_ops.rs_hs;
        d->h2           = op.hi_ops.h2;
        d->r15_modified = true;
        d->place_fn     = &PlaceThumbBranchAndExchange;
        return;
    }

    /* ADD/CMP/MOV with H1/H2 selecting register banks 0-7 vs 8-15. */
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.data_processing.cond      = 0xEu;
    arm_op.data_processing.reserved1 = 0;
    arm_op.data_processing.i         = 0;
    arm_op.data_processing.opcode    = kHiOpsToArm[op.hi_ops.op];
    arm_op.data_processing.rn        = op.hi_ops.rd_hd + 8u * op.hi_ops.h1;
    if (arm_op.data_processing.opcode == 10u) {
        /* CMP — Rd = 0 (flags-only), S = 1. */
        arm_op.data_processing.rd = 0;
        arm_op.data_processing.s  = 1;
    } else {
        arm_op.data_processing.rd = arm_op.data_processing.rn;
        arm_op.data_processing.s  = 0;
    }
    arm_op.data_processing.operand2 = op.hi_ops.rs_hs + 8u * op.hi_ops.h2;
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbAddToStackPointer(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.data_processing.cond      = 0xEu;
    arm_op.data_processing.reserved1 = 0;
    arm_op.data_processing.i         = 1;
    arm_op.data_processing.opcode    = (op.add_to_stack_pointer.s) ? 2 : 4;  /* SUB or ADD */
    arm_op.data_processing.s         = 0;
    arm_op.data_processing.rn        = ArmGpr::kR13;
    arm_op.data_processing.rd        = ArmGpr::kR13;
    uint16_t immediate = op.add_to_stack_pointer.s_word7 * 4u;
    if (immediate > 0xFFu) {
        /* Re-encode as 8-bit imm with ROR 30 (= LSL 2). */
        immediate = static_cast<uint16_t>(
            (15u << 8) | op.add_to_stack_pointer.s_word7);
    }
    arm_op.data_processing.operand2 = immediate;
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbLoadAddress(DecodedInsn* d, ThumbOpcode op) {
    /* SP-relative variant has an exact ARM equivalent (ADD Rd, SP, #imm);
       PC-relative variant does not — Rd = (PC & ~3) + (imm<<2) — and
       routes to a Thumb-specific Place fn that folds the value at emit
       time. */
    if (op.load_address.sp) {
        ArmOpcode arm_op;
        arm_op.word = 0;
        arm_op.data_processing.cond      = 0xEu;
        arm_op.data_processing.reserved1 = 0;
        arm_op.data_processing.i         = 1;
        arm_op.data_processing.opcode    = 4;  /* ADD */
        arm_op.data_processing.s         = 0;
        arm_op.data_processing.rn        = ArmGpr::kR13;
        arm_op.data_processing.rd        = op.load_address.rd;
        if (op.load_address.word8 & 0xC0u) {
            /* Re-encode as 8-bit imm with ROR 30. */
            arm_op.data_processing.operand2 = 0xF00u | op.load_address.word8;
        } else {
            arm_op.data_processing.operand2 = op.load_address.word8 << 2;
        }
        DecodeArm(d, arm_op.word);
    } else {
        d->rd       = op.load_address.rd;
        d->word8    = op.load_address.word8;
        d->place_fn = &PlaceThumbLoadAddressPC;
    }
}
