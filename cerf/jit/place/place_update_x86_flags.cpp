#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceUpdateX86Flags(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx, bool fAdd) {
    using namespace x86;

    if (!d->s) {
        return cursor;
    }

    if (d->rd == ArmGpr::kR15) {
        /* Rd == R15 with S-bit set — copy SPSR to CPSR via
           UpdateCpsrWithFlagsHelper. PUSH SPSR.word, PUSH cpu,
           CALL helper, ADD ESP, 8, RETN (mode may have changed,
           need to exit JIT block). */
        EmitPushBaseDisp32(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, spsr)));
        EmitPush32(cursor,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->Cpu())));
        EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::UpdateCpsrWithFlagsHelper));
        EmitAddRegImm32(cursor, kEsp, 8);
        EmitRetn(cursor, 0);
        return cursor;
    }

    if (d->flags_set) {
        if (d->flags_set == kFlagV) {
            /* SETO AH — 0F 90 ModRM(3, AH, 0). Then store AH to x86_overflow. */
            Emit16(cursor, 0x900F); EmitModRmReg(cursor, 3, kAh, 0);
            EmitMovBaseDisp32Byte(cursor, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, x86_overflow)), kAh);
        } else {
            /* If V is in the set, store SETO byte ptr [&x86_overflow]. */
            if (d->flags_set & kFlagV) {
                /* SETO byte ptr [ESI + offsetof(x86_overflow)] —
                   0F 90 /0 mod=10 r/m=ESI(6) disp32. */
                Emit16(cursor, 0x900F);
                EmitModRmReg(cursor, 2, kStateReg, 0);
                Emit32(cursor,
                    static_cast<uint32_t>(offsetof(ArmCpuState, x86_overflow)));
            }

            if ((d->flags_set & kFlagC) && !fAdd) {
                EmitCmc(cursor);  /* invert CF for SUB-derived ops */
            }

            Emit8(cursor, 0x9F);  /* LAHF */

            if (d->flags_set != kFlagsAll) {
                const uint8_t flag_mask = ArmCpu::GetX86FlagsMask(d);
                if (flag_mask != (kX86FlagZf | kX86FlagCf | kX86FlagNf)) {
                    /* Merge new flag bits into existing x86_flags
                       value (preserve unaffected bits). */
                    EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg,
                        static_cast<int32_t>(offsetof(ArmCpuState, x86_flags)));
                    Emit8(cursor, 0x80); EmitModRmReg(cursor, 3, kAh, 4); Emit8(cursor, flag_mask);
                    Emit8(cursor, 0x80); EmitModRmReg(cursor, 3, kAl, 4); Emit8(cursor, static_cast<uint8_t>(~flag_mask));
                    Emit8(cursor, 0x0A); EmitModRmReg(cursor, 3, kAl, kAh);
                }
            }

            /* MOV [ESI + offsetof(x86_flags)], AH */
            EmitMovBaseDisp32Byte(cursor, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, x86_flags)), kAh);
        }
    }
    return cursor;
}
