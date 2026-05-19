#include <cstddef>

#include "../arm_jit.h"
#include "../arm_mmu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceRaiseAbortPrefetchException(uint8_t*      cursor,
                                          DecodedInsn*  d,
                                          BlockContext* ctx) {
    using namespace x86;

    /* Replay the MMU FAR/FSR snapshot the decoder captured at
       decode time (insn.immediate / insn.reserved3) — written into
       the live ArmMmuState via [EBX + offset]. */
    EmitMovBaseDisp32Imm32(cursor, kMmuReg,
                           static_cast<int32_t>(offsetof(ArmMmuState, fault_address)),
                           d->immediate);
    EmitMovBaseDisp32Imm32(cursor, kMmuReg,
                           static_cast<int32_t>(offsetof(ArmMmuState, fault_status)),
                           d->reserved3);

    /* Same dispatch shape as PlaceRaiseUndefinedException. */
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->Cpu())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::RaiseAbortPrefetchExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* not_in_cache = EmitJzLabel(cursor);
    EmitJmpReg(cursor, kEax);
    FixupLabel(not_in_cache, cursor);
    EmitRetn(cursor, 0);
    return cursor;
}
