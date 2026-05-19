#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceBlxReg(uint8_t*      cursor,
                     DecodedInsn*  d,
                     BlockContext* ctx) {
    using namespace x86;

#if CERF_DEV_MODE
    LOG(Jit, "PlaceBlxReg compile-time pc=0x%08X rd=R%u\n",
        d->guest_address, d->rd);
#endif

    /* MOV [ESI + GPRs[R14]], guest_address + 4 */
    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR14 * 4u),
        d->guest_address + 4u);

    return PlaceBxImpl(cursor, d, ctx, /*is_call=*/true);
}
