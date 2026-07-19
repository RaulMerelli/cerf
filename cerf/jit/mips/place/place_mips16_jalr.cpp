#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* MIPS16 JALR ra, rx (U15509EJ2V0UM Table 3-19 p82): "sets the ISA Mode bit to
   the value in rx bit 0" (branch reads rx before the link); "The address of the
   instruction immediately following the delay slot is placed in register ra"
   (pc+4), "ra bit 0 will reflect the ISA mode bit before the jump" (= 1). */
uint8_t* PlaceMips16Jalr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitBranchRegister(cursor, d->rs, /*mips16=*/true, d->length);
    mips_emit::EmitStoreGprSextImm32(cursor, 31, (d->guest_address + 4u) | 1u);
    return cursor;
}
