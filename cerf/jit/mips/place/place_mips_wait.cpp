#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* cop0 STANDBY (0x21) / SUSPEND (0x22): CPU core clocks freeze until any interrupt
   (VR4102 UM ch.27 p643/p646); RTC+ICU keep running (Table 15-3 p326). Guest
   idle-wait sub_9F038554 @0x9F038554. __fastcall: jit in ECX. */
uint8_t* PlaceMipsWait(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    (void)d;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::WaitHelper));
    return cursor;
}
