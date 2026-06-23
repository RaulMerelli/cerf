#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* MTHI rs: HI = gpr[rs], full 64-bit. QEMU gen_HILO OPC_MTHI (translate.c:2931),
   main accumulator (acc==0): reg!=0 -> tcg_gen_mov_tl(cpu_HI, gpr[rs]); reg==0 ->
   movi 0. gpr[0] reads 0, so the single full-width copy covers rs==0 too. */
uint8_t* PlaceMipsMthi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitMoveGpr64ToField(
        cursor, static_cast<int32_t>(offsetof(MipsCpuState, hi)), d->rs);
    return cursor;
}
