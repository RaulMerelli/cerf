#pragma once

#include <cstdint>

#include "../jit_block.h"

struct MipsDecodedInsn;
struct MipsBlockContext;

using MipsPlaceFn = uint8_t* (*)(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);

struct MipsDecodedInsn {
    MipsPlaceFn place_fn;
    uint32_t    guest_address;   /* raw guest VA of this instruction */
    uint32_t    raw;             /* the original 32-bit instruction word */

    /* R/I/J fields. Bit positions per QEMU translate.c decode_opc:
       op=word>>26, rs=word>>21, rt=word>>16, rd=word>>11, sa=word>>6 (&0x1f),
       funct=word&0x3f, imm=word&0xffff, target=word&0x3ffffff. */
    uint32_t op     : 6;
    uint32_t rs     : 5;
    uint32_t rt     : 5;
    uint32_t rd     : 5;
    uint32_t sa     : 5;
    uint32_t funct  : 6;
    uint32_t imm;                /* raw 16-bit immediate; place fns sign/zero-extend */
    uint32_t target;             /* raw 26-bit jump target */

    /* A branch/jump owns the following instruction (its delay slot), which
       executes regardless of outcome - the translator must emit the pair as a
       unit or control flow is wrong. is_likely squashes the delay slot when the
       branch is not taken (MIPS II+ branch-likely). */
    uint32_t is_branch : 1;
    uint32_t is_likely : 1;

    JitBlock* entry_point;
    uint8_t*  jmp_fixup_location;
};
