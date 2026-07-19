#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* MIPS16 JAL (U15509EJ2V0UM Table 3-19 p82): "The address of the instruction
   immediately following the delay slot is placed in register ra ... The value
   stored in ra bit 0 will reflect the current ISA Mode bit" (= 1); "The ISA
   Mode bit is left unchanged". */
uint8_t* PlaceMips16Jal(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitStoreGprSextImm32(cursor, 31, (d->guest_address + 6u) | 1u);
    mips_emit::EmitBranchUncond(cursor, d->target, 1u, d->length);
    return cursor;
}
