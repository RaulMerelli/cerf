#include <cstdint>

#include "../../../core/cerf_emulator.h"
#include "../arm_jit.h"
#include "../arm_vfp.h"
#include "../decoded_insn.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

uint8_t* EmitVfpDataOperation(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;

    const uint32_t packed =
        ((d->cp_opc & 0xFu) << 19) |
        ((d->crn    & 0xFu) << 15) |
        ((d->crd    & 0xFu) << 11) |
        ((d->cp_num & 0xFu) <<  7) |
        ((d->cp     & 0x7u) <<  4) |
         (d->crm    & 0xFu);

    EmitPush32(cursor, packed);
    EmitPush32(cursor, d->guest_address);
    EmitMovRegImm32(cursor, kEax,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(jit->Vfp())));
    EmitPushReg(cursor, kEax);
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmVfp::ExecuteCdpHelper));
    EmitAddRegImm32(cursor, kEsp, 12);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* fall_through = EmitJzLabel(cursor);
    EmitRetn(cursor, 0);
    FixupLabel(fall_through, cursor);
    return cursor;
}
