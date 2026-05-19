#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceSingleDataTransferOffset(uint8_t*           cursor,
                                       const DecodedInsn* d,
                                       BlockContext*      ctx,
                                       bool*              needs_alignment_check) {
    using namespace x86;

    /* LDRB/STRB (byte transfer) needs no alignment check; word
       LDR/STR starts pessimistic and the immediate-offset PC-
       relative fast path below clears it when the final address
       is compile-time word-aligned. */
    if (d->b) {
        *needs_alignment_check = false;
    } else {
        *needs_alignment_check = true;
    }

    if (d->i) {
        /* Register-form Operand2 — run the shifter, then add/sub
           the result against the base. EDX receives the shifted
           value (PlaceDecodedShift output target). */
        cursor = PlaceDecodedShift(cursor, d, ctx, kEdx, /*needs_sco=*/false);

        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, kEcx,
                d->guest_address +
                (ctx->jit->CpuState()->cpsr.bits.thumb_mode ? 4u : 8u));
        } else {
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4));
        }

        if (d->p) {
            /* Pre-indexed: EA = base ± shifted-Rm; write EA to ECX
               (and, if W=1, to EBP for the caller's writeback). */
            if (d->u) {
                /* ADD ECX, EDX — 0x03 /r ModRM(3, EDX, ECX). */
                Emit8(cursor, 0x03); EmitModRmReg(cursor, 3, kEdx, kEcx);
            } else {
                /* SUB ECX, EDX — 0x2B /r ModRM(3, EDX, ECX). */
                Emit8(cursor, 0x2B); EmitModRmReg(cursor, 3, kEdx, kEcx);
            }
            if (d->w) {
                /* MOV EBP, ECX — caller stores EBP into Cpu.GPRs[Rn]. */
                EmitMovRegReg(cursor, kEbp, kEcx);
            }
        } else {
            /* Post-indexed: ECX holds base for the access; EA-after
               (= base ± shifted-Rm) lands in EBP via LEA so the
               caller can write it back to Rn. */
            if (d->u == 0) {
                /* Down direction: NEG EDX so the LEA below adds the
                   negated value. NEG r32 — 0xF7 /3 ModRM(3, EDX, 3). */
                Emit8(cursor, 0xF7); EmitModRmReg(cursor, 3, kEdx, 3);
            }
            /* LEA EBP, [ECX + EDX] — 0x8D mod=0 r/m=4(SIB) reg=EBP;
               SIB ss=0 index=EDX base=ECX. */
            Emit8(cursor, 0x8D);
            EmitModRmReg(cursor, 0, 4, kEbp);
            EmitSib(cursor, 0, kEdx, kEcx);
        }
    } else {
        /* Immediate-form Operand2 (d->offset already decoded). */
        if (d->rn == ArmGpr::kR15) {
            /* PC-relative load/store — the decoder pre-computed
               R15 ± Offset and stashed it in reserved3. Move that
               literal into ECX as the effective address. */
            EmitMovRegImm32(cursor, kEcx, d->reserved3);
            if ((d->reserved3 & 3u) == 0u) {
                /* Address is compile-time word-aligned, skip the
                   runtime alignment check the caller would emit. */
                *needs_alignment_check = false;
            }
        } else {
            /* MOV ECX, [ESI + offsetof(gprs[Rn])]. */
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4));

            if (d->p) {
                /* Pre-indexed: ECX = base ± Offset. */
                if (d->u) {
                    EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
                } else {
                    EmitSubRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
                }
                if (d->w) {
                    /* Writeback enabled — stage EA in EBP for the
                       caller's store-back to Rn. */
                    EmitMovRegReg(cursor, kEbp, kEcx);
                }
            } else {
                /* Post-indexed: ECX = base for the access; EA-after
                   = base ± Offset goes into EBP via LEA. */
                const uint32_t offset_to_add =
                    d->u ? static_cast<uint32_t>(d->offset)
                         : static_cast<uint32_t>(-d->offset);

                /* If the offset fits in a signed int8, emit the
                   compact LEA disp8 form; else disp32. */
                if (static_cast<uint32_t>(static_cast<int8_t>(offset_to_add)) ==
                    offset_to_add) {
                    /* LEA EBP, [ECX + disp8] — 0x8D mod=1 r/m=ECX reg=EBP disp8. */
                    Emit8(cursor, 0x8D);
                    EmitModRmReg(cursor, 1, kEcx, kEbp);
                    Emit8(cursor, static_cast<uint8_t>(offset_to_add));
                } else {
                    /* LEA EBP, [ECX + disp32] — 0x8D mod=2 r/m=ECX reg=EBP disp32. */
                    Emit8(cursor, 0x8D);
                    EmitModRmReg(cursor, 2, kEcx, kEbp);
                    Emit32(cursor, offset_to_add);
                }
            }
        }
    }
    return cursor;
}
