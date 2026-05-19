#include "arm_decoder.h"

#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "thumb_opcode.h"

void ArmDecoder::DecodeThumbLoadStoreRegisterOffset(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.single_data_transfer.cond      = 0xEu;
    arm_op.single_data_transfer.reserved1 = 1u;
    arm_op.single_data_transfer.i         = 1;  /* register offset */
    arm_op.single_data_transfer.p         = 1;  /* pre-index */
    arm_op.single_data_transfer.u         = 1;  /* up */
    arm_op.single_data_transfer.b         = op.load_store_register_offset.b;
    arm_op.single_data_transfer.w         = 0;  /* no writeback */
    arm_op.single_data_transfer.l         = op.load_store_register_offset.l;
    arm_op.single_data_transfer.rn        = op.load_store_register_offset.rb;
    arm_op.single_data_transfer.rd        = op.load_store_register_offset.rd;
    arm_op.single_data_transfer.offset    = op.load_store_register_offset.ro;
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbLoadStoreByteHalfWord(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.half_word_signed_transfer_register.cond      = 0xEu;
    arm_op.half_word_signed_transfer_register.reserved4 = 0;
    arm_op.half_word_signed_transfer_register.p         = 1;  /* pre-index */
    arm_op.half_word_signed_transfer_register.u         = 1;
    arm_op.half_word_signed_transfer_register.reserved3 = 0;
    arm_op.half_word_signed_transfer_register.w         = 0;
    arm_op.half_word_signed_transfer_register.h         = op.load_store_byte_half_word.h;
    arm_op.half_word_signed_transfer_register.s         = op.load_store_byte_half_word.s;
    if (arm_op.half_word_signed_transfer_register.h == 0 &&
        arm_op.half_word_signed_transfer_register.s == 0) {
        /* Thumb encodes STRH with H=S=0; ARM encodes STRH as
           H=1, L=0. Re-map. */
        arm_op.half_word_signed_transfer_register.l = 0;
        arm_op.half_word_signed_transfer_register.h = 1;
    } else {
        arm_op.half_word_signed_transfer_register.l = 1;
    }
    arm_op.half_word_signed_transfer_register.rn        = op.load_store_byte_half_word.rb;
    arm_op.half_word_signed_transfer_register.rd        = op.load_store_byte_half_word.rd;
    arm_op.half_word_signed_transfer_register.reserved2 = 1;
    arm_op.half_word_signed_transfer_register.reserved1 = 1;
    arm_op.half_word_signed_transfer_register.rm        = op.load_store_byte_half_word.ro;
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbLoadStoreImmediateOffset(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.single_data_transfer.cond      = 0xEu;
    arm_op.single_data_transfer.reserved1 = 1u;
    arm_op.single_data_transfer.i         = 0;  /* immediate offset */
    arm_op.single_data_transfer.p         = 1;
    arm_op.single_data_transfer.u         = 1;
    arm_op.single_data_transfer.b         = op.load_store_immediate_offset.b;
    arm_op.single_data_transfer.w         = 0;
    arm_op.single_data_transfer.l         = op.load_store_immediate_offset.l;
    arm_op.single_data_transfer.rn        = op.load_store_immediate_offset.rb;
    arm_op.single_data_transfer.rd        = op.load_store_immediate_offset.rd;
    /* Byte accesses use offset5 unmodified; word accesses scale by 4. */
    arm_op.single_data_transfer.offset = op.load_store_immediate_offset.b
        ? op.load_store_immediate_offset.offset5
        : op.load_store_immediate_offset.offset5 << 2;
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbLoadStoreHalfWord(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.half_word_signed_transfer_immediate.cond        = 0xEu;
    arm_op.half_word_signed_transfer_immediate.reserved4   = 0;
    arm_op.half_word_signed_transfer_immediate.p           = 1;
    arm_op.half_word_signed_transfer_immediate.u           = 1;
    arm_op.half_word_signed_transfer_immediate.reserved3   = 1;
    arm_op.half_word_signed_transfer_immediate.w           = 0;
    arm_op.half_word_signed_transfer_immediate.l           = op.load_store_half_word.l;
    arm_op.half_word_signed_transfer_immediate.rn          = op.load_store_half_word.rb;
    arm_op.half_word_signed_transfer_immediate.rd          = op.load_store_half_word.rd;
    arm_op.half_word_signed_transfer_immediate.offset_high = op.load_store_half_word.offset5 >> 3;
    arm_op.half_word_signed_transfer_immediate.reserved2   = 1;
    arm_op.half_word_signed_transfer_immediate.s           = 0;
    arm_op.half_word_signed_transfer_immediate.h           = 1;
    arm_op.half_word_signed_transfer_immediate.reserved1   = 1;
    arm_op.half_word_signed_transfer_immediate.offset_low  = (op.load_store_half_word.offset5 & 0x7u) << 1;
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbLoadStoreSPRelative(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.single_data_transfer.cond      = 0xEu;
    arm_op.single_data_transfer.reserved1 = 1u;
    arm_op.single_data_transfer.i         = 0;
    arm_op.single_data_transfer.p         = 1;
    arm_op.single_data_transfer.u         = 1;
    arm_op.single_data_transfer.b         = 0;
    arm_op.single_data_transfer.w         = 0;
    arm_op.single_data_transfer.l         = op.load_store_sp_relative.l;
    arm_op.single_data_transfer.rn        = ArmGpr::kR13;
    arm_op.single_data_transfer.rd        = op.load_store_sp_relative.rd;
    arm_op.single_data_transfer.offset    = op.load_store_sp_relative.word8 << 2;
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbPushPopRegisters(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.block_data_transfer.cond      = 0xEu;
    arm_op.block_data_transfer.reserved1 = 4u;
    arm_op.block_data_transfer.s         = 0;
    arm_op.block_data_transfer.w         = 1;  /* writeback */
    arm_op.block_data_transfer.l         = op.push_pop_registers.l;
    if (op.push_pop_registers.l) {
        /* POP = LDMIA. */
        arm_op.block_data_transfer.p = 0;
        arm_op.block_data_transfer.u = 1;
    } else {
        /* PUSH = STMDB. */
        arm_op.block_data_transfer.p = 1;
        arm_op.block_data_transfer.u = 0;
    }
    arm_op.block_data_transfer.rn            = ArmGpr::kR13;
    arm_op.block_data_transfer.register_list = op.push_pop_registers.r_list;
    if (op.push_pop_registers.r) {
        if (op.push_pop_registers.l) {
            /* POP — include R15 (PC). */
            arm_op.block_data_transfer.register_list |= 1u << ArmGpr::kR15;
        } else {
            /* PUSH — include R14 (LR). */
            arm_op.block_data_transfer.register_list |= 1u << ArmGpr::kR14;
        }
    }
    DecodeArm(d, arm_op.word);
}

void ArmDecoder::DecodeThumbMultipleLoadStore(DecodedInsn* d, ThumbOpcode op) {
    ArmOpcode arm_op;
    arm_op.word = 0;
    arm_op.block_data_transfer.cond          = 0xEu;
    arm_op.block_data_transfer.reserved1     = 4u;
    arm_op.block_data_transfer.s             = 0;
    arm_op.block_data_transfer.w             = 1;
    /* Generate LDMIA / STMIA. */
    arm_op.block_data_transfer.l             = op.multiple_load_store.l;
    arm_op.block_data_transfer.p             = 0;
    arm_op.block_data_transfer.u             = 1;
    arm_op.block_data_transfer.rn            = op.multiple_load_store.rb;
    arm_op.block_data_transfer.register_list = op.multiple_load_store.r_list;
    DecodeArm(d, arm_op.word);
}
