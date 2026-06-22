#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* BGTZ rs, offset: branch if (int64)gpr[rs] > 0 (delay slot runs regardless).
   The 64-bit test must not collapse to a 32-bit signed lo>0: a value with
   hi==0 and lo bit31 set is +2^31 (>0), which a 32-bit test would call <0. */
uint8_t* PlaceMipsBgtz(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t  pc_off = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    const uint32_t fall = d->guest_address + 8u;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* j_fall_hi_neg = EmitJsLabel32(cursor);  /* hi<0  -> value<0  -> fall */
    uint8_t* j_take_hi_pos = EmitJnzLabel(cursor);   /* hi>0  -> value>0  -> take */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* j_fall_zero = EmitJzLabel(cursor);      /* hi==0 && lo==0 -> ==0 -> fall */

    uint8_t* take_label = cursor;                    /* hi==0 && lo!=0, or jnz landing */
    EmitMovBaseDisp32Imm32(cursor, kStateReg, pc_off, btgt);
    uint8_t* j_done = EmitJmpLabel(cursor);

    uint8_t* fall_label = cursor;
    EmitMovBaseDisp32Imm32(cursor, kStateReg, pc_off, fall);

    uint8_t* done_label = cursor;
    FixupLabel(j_take_hi_pos, take_label);
    FixupLabel(j_fall_zero,   fall_label);
    FixupLabel32(j_fall_hi_neg, fall_label);
    FixupLabel(j_done, done_label);
    return cursor;
}
