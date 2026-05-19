#include "arm_decoder.h"

#include "../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb_opcode.h"

bool ArmDecoder::DecodeThumb(DecodedInsn* insn, uint16_t opcode_word) {
    ThumbOpcode op;
    op.half_word = opcode_word;

    insn->cond = 14;

    switch (op.generic.opcode) {
    case 0:
        DecodeThumbMoveAddSub(insn, op);
        break;

    case 1:
        DecodeThumbMathImmediate(insn, op);
        break;

    case 2:
        DecodeThumbCase2(insn, op);
        break;

    case 3:
        DecodeThumbLoadStoreImmediateOffset(insn, op);
        break;

    case 4:
        if (op.load_store_half_word.reserved1 == 0) {
            DecodeThumbLoadStoreHalfWord(insn, op);
        } else {
            DecodeThumbLoadStoreSPRelative(insn, op);
        }
        break;

    case 5:
        if (op.load_address.reserved1 == 0) {
            DecodeThumbLoadAddress(insn, op);
        } else if (op.add_to_stack_pointer.reserved1 == 0x10u) {
            DecodeThumbAddToStackPointer(insn, op);
        } else if (op.add_to_stack_pointer.reserved1 == 0x1Eu) {
            insn->place_fn = &PlaceBKPT;
        } else if (op.push_pop_registers.reserved2 == 1u &&
                   op.push_pop_registers.reserved1 == 2u) {
            DecodeThumbPushPopRegisters(insn, op);
        } else {
            insn->place_fn = &PlaceRaiseUndefinedException;
        }
        break;

    case 6:
        if (op.multiple_load_store.reserved1 == 0u) {
            DecodeThumbMultipleLoadStore(insn, op);
        } else {
            /* This branch also handles software interrupt (cond ==
               0xF) and the "break" escape (cond == 0xE). */
            DecodeThumbConditionalBranch(insn, op);
        }
        break;

    case 7:
        insn->cond       = 14;
        insn->h_two_bits = op.long_branch.h;
        switch (insn->h_two_bits) {
        case 0:
            /* Unconditional long branch — signed 11-bit offset
               scaled << 1. */
            insn->offset       = 2 * op.unconditional_branch.offset11;
            insn->r15_modified = true;
            break;
        case 2:
            /* BL high half — 11-bit offset shifted left 12 (range
               ±4 MB). Sign-extend the 10-bit value to 32 bits. */
            if (op.long_branch.offset & 0x400u) {
                insn->offset = static_cast<int32_t>(
                    (0xFFFFFC00u | op.long_branch.offset) << 12);
            } else {
                insn->offset = static_cast<int32_t>(
                    op.long_branch.offset << 12);
            }
            break;
        case 1:  /* BLX low half — Thumb→ARM transition. */
        case 3:  /* BL low half — Thumb→Thumb. */
            insn->offset       = op.long_branch.offset << 1;
            insn->r15_modified = true;
            break;
        default:
            break;
        }
        insn->place_fn = &PlaceThumbLongBranch;
        break;

    default:
        break;
    }

    return insn->place_fn != &PlaceRaiseUndefinedException;
}

/* === Sub-dispatcher for opcode class 2 (ALU / Hi / PC-relative /
       load-store-register-offset / load-store-byte-half-word). === */
void ArmDecoder::DecodeThumbCase2(DecodedInsn* d, ThumbOpcode op) {
    switch (op.alu_operation.reserved) {
    case 0:
        DecodeThumbALUOperation(d, op);
        break;
    case 1:
        DecodeThumbHiOps(d, op);
        break;
    case 2:
    case 3:
        DecodeThumbPCRelativeLoad(d, op);
        break;
    default:
        if (op.load_store_register_offset.reserved1 == 0u) {
            DecodeThumbLoadStoreRegisterOffset(d, op);
        } else {
            DecodeThumbLoadStoreByteHalfWord(d, op);
        }
        break;
    }
}

/* === Conditional branch / SWI / break (opcode class 6 with
       reserved1==1). === */
void ArmDecoder::DecodeThumbConditionalBranch(DecodedInsn* d, ThumbOpcode op) {
    if (op.conditional_branch.cond == 0xFu) {
        /* SWI — synthesize ARM SoftwareInterrupt encoding. */
        ArmOpcode arm_op;
        arm_op.word = 0;
        arm_op.software_interrupt.cond      = 0xEu;
        arm_op.software_interrupt.reserved1 = 0xFu;
        if (processor_config_->GenerateSyscalls() &&
            op.software_interrupt.value8 == 0xABu) {
            arm_op.software_interrupt.ignored = 0x123456u;
        } else {
            arm_op.software_interrupt.ignored = op.software_interrupt.value8;
        }
        DecodeArm(d, arm_op.word);
    } else if (op.conditional_branch.cond == 0xEu) {
        /* "break" opcode (not BKPT) — used by CE's assert mechanism
           as a deliberate UND-fault trigger. */
        d->place_fn = &PlaceRaiseUndefinedException;
    } else {
        /* Conditional branch. Offset is sign-extended 8-bit halfword
           displacement; final R15 = guest_address + 4 + 2*soffset8
           (the +4 accounts for the 1-instruction Thumb prefetch). */
        d->cond         = op.conditional_branch.cond;
        d->offset       = d->guest_address +
                          2 * op.conditional_branch.s_offset8 + 4;
        d->l            = 0;
        d->r15_modified = true;
        d->place_fn     = &PlaceBranch;
    }
}

/* === PC-relative load — LDR Rd, [PC + #imm]. === */
void ArmDecoder::DecodeThumbPCRelativeLoad(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.single_data_transfer.cond      = 0xEu;
    arm_op.single_data_transfer.reserved1 = 1u;
    arm_op.single_data_transfer.i         = 0;  /* immediate offset */
    arm_op.single_data_transfer.p         = 1;  /* pre-index */
    arm_op.single_data_transfer.u         = 1;  /* up */
    arm_op.single_data_transfer.b         = 0;  /* word */
    arm_op.single_data_transfer.w         = 0;  /* no writeback */
    arm_op.single_data_transfer.l         = 1;  /* load */
    arm_op.single_data_transfer.rn        = ArmGpr::kR15;
    arm_op.single_data_transfer.rd        = op.pc_relative_load.rd;
    arm_op.single_data_transfer.offset    = op.pc_relative_load.word8 << 2;
    if ((d->guest_address + 4u) & 2u) {
        /* Force PC bit[2] clear by trimming the offset; ARM PC-relative
           LDR always treats PC as 4-byte aligned. */
        arm_op.single_data_transfer.offset -= 2;
    }
    DecodeArm(d, arm_op.word);
}
