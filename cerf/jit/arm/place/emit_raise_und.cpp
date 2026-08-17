#include <cstdint>
#include <cstring>

#include "../arm_cpu.h"
#include "../arm_emit_services.h"
#include "../arm_mmu_probe.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"
#include "../../../core/log.h"

uint8_t* EmitRaiseUndTail(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, d->length);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmCpu::RaiseUndefinedExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 12);
    EmitRet(cursor);
    return cursor;
}

uint8_t* EmitRaiseUndAndReturn(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    ArmEmitServices* emit = ctx->emit;
    /* Re-read the guest word the decoder choked on (read-only MMU peek, same
       page the fetch used) so the log distinguishes corrupt/poison bytes from a
       genuine unsupported encoding. */
    const ArmCpuState* cs = emit->Cpu()->State();
    uint32_t word = 0xFFFFFFFFu;
    bool word_ok = false;
    if (uint8_t* p = emit->MmuProbe()->PeekVaToHost(d->actual_guest_address)) {
        word = 0u;
        std::memcpy(&word, p, cs->cpsr.bits.thumb_mode ? 2u : 4u);
        word_ok = true;
    }
    LOG(Jit, "EmitRaiseUndAndReturn: UNDEFINED encoding at pc=0x%08X (actual=0x%08X) "
             "word=%s0x%08X T=%u mode=0x%02X\n",
        d->guest_address, d->actual_guest_address,
        word_ok ? "" : "UNMAPPED:", word,
        cs->cpsr.bits.thumb_mode, cs->cpsr.bits.mode);
    return EmitRaiseUndTail(cursor, d, ctx);
}
