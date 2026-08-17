#include <cstddef>
#include <cstdint>

#include "../../../core/log.h"
#include "../../../cpu/arm_processor_config.h"
#include "../arm_emit_services.h"
#include "../arm_mmu.h"
#include "../arm_mmu_state.h"
#include "../arm_routed_access.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

/* Shift(R[m], shift_t, shift_n, APSR.C) with DecodeImmShift (DDI 0406C.c
   A8.4, p. A8-291: LSR/ASR imm5 == 0 encode a shift by 32, ROR imm5 == 0
   encodes RRX) into EAX; ECX is scratch, EDX is preserved. */
void EmitShiftedOffsetIntoEax(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    switch (d->op1) {
    case 0u:
        if (d->rs != 0u) {
            EmitShlReg32Imm(cursor, kEax, static_cast<uint8_t>(d->rs));
        }
        break;
    case 1u:
        if (d->rs != 0u) {
            EmitShrReg32Imm(cursor, kEax, static_cast<uint8_t>(d->rs));
        } else {
            EmitMovRegImm32(cursor, kEax, 0u);
        }
        break;
    case 2u:
        EmitSarReg32Imm(cursor, kEax,
                        static_cast<uint8_t>(d->rs != 0u ? d->rs : 31u));
        break;
    default:
        if (d->rs != 0u) {
            EmitRorReg32Imm(cursor, kEax, static_cast<uint8_t>(d->rs));
        } else {
            /* RRX_C (DDI 0406C.c A2.2.1, p. A2-43): result = carry_in :
               x<31:1>; RCR by 1 with CF = APSR.C. */
            EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
            EmitCmc(cursor);
            EmitRcrReg32By1(cursor, kEax);
        }
        break;
    }
}

/* offset_addr = R[n] +/- offset (A8.8.63 / A8.8.66 encoding-specific
   operations) into ECX; EAX/ECX are scratch, EDX is preserved. */
void EmitOffsetAddrIntoEcx(uint8_t*& cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    if (d->n) {
        if (d->rn == 15u) {
            EmitMovRegImm32(cursor, kEcx,
                ArmPcReadValue(d, ctx) + static_cast<uint32_t>(d->offset));
        } else {
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
            if (d->offset != 0) {
                EmitAddRegImm32(cursor, kEcx,
                                static_cast<uint32_t>(d->offset));
            }
        }
        return;
    }
    EmitShiftedOffsetIntoEax(cursor, d);
    if (d->rn == 15u) {
        EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    }
    if (d->u) {
        EmitAddReg32Reg32(cursor, kEcx, kEax);
    } else {
        EmitSubReg32Reg32(cursor, kEcx, kEax);
    }
}

void EmitWritebackRecomputed(uint8_t*& cursor, DecodedInsn* d,
                             BlockContext* ctx) {
    using namespace x86;
    EmitOffsetAddrIntoEcx(cursor, d, ctx);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rn), kEcx);
}

/* R[n] = offset_addr (A8.8.63 operation order: after the access, before
   the Rt write). EDX carries the load's Rt data across this tail - only
   EAX/ECX are scratch. */
void EmitWritebackTail(uint8_t*& cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    if (d->p) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rn), kEcx);
        return;
    }
    EmitWritebackRecomputed(cursor, d, ctx);
}

}  /* namespace */

/* ARM DDI 0406C.c Table A5-15 (p. A5-208). LDR A8.8.63 (p. A8-408), LDR
   (literal) A8.8.64 (p. A8-410), LDR (register) A8.8.66 (p. A8-414), STR
   (immediate) p. A8-674, STR (register) p. A8-676, LDRB pp. A8-418/422,
   STRB pp. A8-680/682. */
uint8_t* PlaceSingleDataTransfer(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    using namespace x86;
    const ArmProcessorConfig* config = ctx->emit->ProcessorConfig();
    ArmMmu*                   mmu    = ctx->emit->Mmu();
    const ArmSctlr            sctlr  = mmu->State()->effective_control_register;
    const uint32_t            mmu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu));

    const bool imm_form = d->n != 0;
    const bool is_byte  = d->s != 0;
    const bool load     = d->l != 0;
    const bool wback    = !d->p || d->w;

    /* Table A5-15 (p. A5-208): P == 0 && W == 1 selects the unprivileged
       forms only in the ARM encoding. Thumb-2 A8.8.203 STR (immediate) T4
       and its load/byte variants use the same P/W pair for ordinary
       post-indexing. */
    const bool unpriv = !d->thumb_encoding && !d->p && d->w;

    /* A8.8.63: wback && n == t; A8.8.64: P == W; A8.8.66 / STR (register)
       p. A8-676: m == 15, wback && (n == 15 || n == t), ArchVersion() < 6
       && wback && m == n; LDRB p. A8-418 / STRB p. A8-680: t == 15; LDRT
       DDI 0100I A4.1.31 (p. A4-60): load Rd == 15. Every UNPREDICTABLE
       case is implemented as UNDEFINED (p. Glossary-2737). */
    bool unpredictable = false;
    if (unpriv && load && d->rd == 15u) {
        unpredictable = true;
    }
    if (is_byte && d->rd == 15u) {
        unpredictable = true;
    }
    if (wback && (d->rn == 15u || d->rn == d->rd)) {
        unpredictable = true;
    }
    if (!imm_form) {
        if (d->rm == 15u) {
            unpredictable = true;
        }
        if (wback && !config->HasCp15V6() && !config->HasCp15V7() &&
            d->rm == d->rn) {
            unpredictable = true;
        }
    }
    if (unpredictable) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* address = offset_addr (P == 1) or R[n] (P == 0). */
    if (d->p) {
        EmitOffsetAddrIntoEcx(cursor, d, ctx);
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    }

    /* Table A3-1 (p. A3-108): LDR/STR word check - SCTLR.A == 1 Alignment
       fault, A == 0 unaligned access; D15.3.1 (p. D15-2592): v4/v5 and the
       v6 SCTLR.U == 0 legacy configuration rotate an unaligned LDR and
       ignore address[1:0] on an unaligned STR. */
    const bool legacy = !mmu->UnalignedAccessesFault();

    uint8_t* align_fault_label = nullptr;
    uint8_t* legacy_label      = nullptr;
    uint8_t* cross_label       = nullptr;
    if (!is_byte) {
        if (sctlr.bits.a) {
            EmitTestRegImm32(cursor, kEcx, 3u);
            align_fault_label = EmitJnzLabel32(cursor);
        } else if (legacy) {
            EmitTestRegImm32(cursor, kEcx, 3u);
            legacy_label = EmitJnzLabel32(cursor);
        } else {
            /* An unaligned word can span two mappings at a 1 KB boundary -
               Tiny pages map 1 KB (Table D15-10, p. D15-2609; A3.2.3,
               p. A3-109). */
            EmitMovRegReg  (cursor, kEdx, kEcx);
            EmitAndRegImm32(cursor, kEdx, 0x3FFu);
            EmitAddRegImm32(cursor, kEdx, 3u);
            EmitTestRegImm32(cursor, kEdx, 0x400u);
            cross_label = EmitJnzLabel32(cursor);
        }
    }

    uint8_t* abort_labels[3];
    int      n_abort = 0;
    uint8_t* fault_labels[2];
    int      n_fault = 0;
    uint8_t* done_labels[3];
    int      n_done = 0;

    cursor = EmitTranslateAccess(cursor, ctx,
                                 load ? TlbAccess::kRead : TlbAccess::kWrite,
                                 unpriv);
    EmitTestRegReg(cursor, kEax, kEax);
    abort_labels[n_abort++] = EmitJzLabel32(cursor);

    if (load) {
        if (is_byte) {
            /* MOVZX EDX, byte [EAX] - 0F B6 /r (SDM Vol. 2B 4-140 MOVZX). */
            Emit8(cursor, 0x0F);
            Emit8(cursor, 0xB6);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
        } else {
            /* MOV EDX, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x8B);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
        }
        if (wback) {
            EmitWritebackTail(cursor, d, ctx);
        }
        if (d->rd == 15u) {
            EmitMovRegReg(cursor, kEax, kEdx);
            cursor = EmitLoadedPcWrite(cursor, d, ctx);
        } else {
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    } else {
        if (d->rd == 15u) {
            /* PCStoreValue (A2.3.2, p. A2-47): the stored value is the
               instruction address plus 8 or plus 12 per the core. */
            EmitMovRegImm32(cursor, kEdx,
                d->guest_address + config->PcStoreOffset());
        } else {
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, GprDisp(d->rd));
        }
        if (is_byte) {
            /* MOV [EAX], DL - 88 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x88);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
        } else {
            /* MOV [EAX], EDX - 89 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
        }
        if (wback) {
            EmitWritebackTail(cursor, d, ctx);
        }
        done_labels[n_done++] = EmitJmpLabel32(cursor);
    }

    /* .legacy_unaligned (D15.3.1, p. D15-2592): ECX = EA. */
    if (legacy_label != nullptr) {
        FixupLabel32(legacy_label, cursor);
        if (load && d->rd == 15u) {
            /* A3.2.2 (p. A3-109): an unaligned PC load is UNPREDICTABLE. */
            cursor = EmitRaiseUndTail(cursor, d, ctx);
        } else if (load) {
            EmitMovRegReg  (cursor, kEdx, kEcx);
            EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
            EmitPushReg(cursor, kEdx);
            cursor = EmitTranslateAccess(cursor, ctx, TlbAccess::kRead, unpriv);
            EmitPopReg(cursor, kEdx);
            EmitTestRegReg(cursor, kEax, kEax);
            fault_labels[n_fault++] = EmitJzLabel32(cursor);
            /* MOV EAX, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x8B);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEax);
            EmitMovRegReg  (cursor, kEcx, kEdx);
            EmitAndRegImm32(cursor, kEcx, 3u);
            EmitShlReg32Imm(cursor, kEcx, 3u);
            /* ROR EAX, CL - D3 /1 (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
            EmitRorReg32Cl(cursor, kEax);
            EmitMovRegReg(cursor, kEdx, kEax);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        } else {
            EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
            cursor = EmitTranslateAccess(cursor, ctx, TlbAccess::kWrite, unpriv);
            EmitTestRegReg(cursor, kEax, kEax);
            abort_labels[n_abort++] = EmitJzLabel32(cursor);
            if (d->rd == 15u) {
                EmitMovRegImm32(cursor, kEdx,
                    d->guest_address + config->PcStoreOffset());
            } else {
                EmitMovRegBaseDisp32(cursor, kEdx, kStateReg,
                                     GprDisp(d->rd));
            }
            /* MOV [EAX], EDX - 89 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    }

    /* .cross: ECX = EA. */
    if (cross_label != nullptr) {
        FixupLabel32(cross_label, cursor);
        if (load && d->rd == 15u) {
            /* A3.2.2 (p. A3-109): an unaligned PC load is UNPREDICTABLE. */
            cursor = EmitRaiseUndTail(cursor, d, ctx);
        } else if (load) {
            EmitPush32 (cursor, unpriv ? 1u : 0u);
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, mmu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmMmu::UnalignedWordLoadHelper));
            EmitAddRegImm32(cursor, kEsp, 12u);
            EmitTestRegReg(cursor, kEdx, kEdx);
            fault_labels[n_fault++] = EmitJzLabel32(cursor);
            EmitMovRegReg(cursor, kEdx, kEax);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        } else {
            if (d->rd == 15u) {
                EmitMovRegImm32(cursor, kEdx,
                    d->guest_address + config->PcStoreOffset());
            } else {
                EmitMovRegBaseDisp32(cursor, kEdx, kStateReg,
                                     GprDisp(d->rd));
            }
            EmitPush32 (cursor, unpriv ? 1u : 0u);
            EmitPushReg(cursor, kEdx);
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, mmu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmMmu::UnalignedWordStoreHelper));
            EmitAddRegImm32(cursor, kEsp, 16u);
            EmitCmpRegImm32(cursor, kEax, 0xFFFFFFFFu);
            fault_labels[n_fault++] = EmitJzLabel32(cursor);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    }

    /* .align_fault: ECX = EA. ddi0406c D15.5.2 Base Updated Abort Model
       (p. D15-2604): "the base register of any valid load/store instruction
       that causes a memory system abort is modified by the base register
       writeback, if any, of that instruction". */
    const bool base_updated_abort =
        wback && !config->BaseRestoredAbortModel();
    uint8_t* align_done_label = nullptr;
    if (align_fault_label != nullptr) {
        FixupLabel32(align_fault_label, cursor);
        EmitMovRegImm32(cursor, kEdx, mmu_imm);
        EmitCall(cursor, load
            ? reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper)
            : reinterpret_cast<void*>(&ArmMmu::AlignmentFaultWriteHelper));
        if (base_updated_abort) {
            EmitWritebackRecomputed(cursor, d, ctx);
            align_done_label = EmitJmpLabel32(cursor);
        }
    }

    /* .abort: */
    for (int i = 0; i < n_abort; ++i) {
        FixupLabel32(abort_labels[i], cursor);
    }
    /* 8B /r mod=00 disp32 (SDM Vol. 2B 4-35 MOV). */
    Emit8(cursor, 0x8B);
    EmitModRmDisp32(cursor, kEax);
    Emit32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(mmu->IoPendingValidPtr())));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* real_fault_label = EmitJzLabel32(cursor);

    const uint32_t routed_imm = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->RoutedAccess()));
    const uint32_t io_bytes = is_byte ? 1u : 4u;
    if (load) {
        EmitPushReg(cursor, kEcx);
        EmitPush32 (cursor, d->guest_address);
        EmitPush32 (cursor, io_bytes);
        EmitPush32 (cursor, routed_imm);
        EmitCall(cursor,
            reinterpret_cast<void*>(&ArmRoutedAccess::IoLoadHelper));
        EmitAddRegImm32(cursor, kEsp, 16u);
        EmitMovRegReg(cursor, kEdx, kEax);
        if (wback) {
            EmitWritebackRecomputed(cursor, d, ctx);
        }
        if (d->rd == 15u) {
            EmitMovRegReg(cursor, kEax, kEdx);
            cursor = EmitLoadedPcWrite(cursor, d, ctx);
        } else {
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    } else {
        if (d->rd == 15u) {
            EmitMovRegImm32(cursor, kEdx,
                d->guest_address + config->PcStoreOffset());
        } else {
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, GprDisp(d->rd));
        }
        EmitPushReg(cursor, kEdx);
        EmitPushReg(cursor, kEcx);
        EmitPush32 (cursor, d->guest_address);
        EmitPush32 (cursor, io_bytes);
        EmitPush32 (cursor, routed_imm);
        EmitCall(cursor,
            reinterpret_cast<void*>(&ArmRoutedAccess::IoStoreHelper));
        EmitAddRegImm32(cursor, kEsp, 20u);
        if (wback) {
            EmitWritebackRecomputed(cursor, d, ctx);
        }
        done_labels[n_done++] = EmitJmpLabel32(cursor);
    }

    FixupLabel32(real_fault_label, cursor);
    for (int i = 0; i < n_fault; ++i) {
        FixupLabel32(fault_labels[i], cursor);
    }
    if (base_updated_abort) {
        /* ARM DDI 0406C.c D15.5.2 (p. D15-2604) Base Updated Abort Model
           applies to an instruction "that causes a memory system abort". */
        /* 8B /r mod=00 disp32 (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x8B);
        EmitModRmDisp32(cursor, kEax);
        Emit32(cursor, static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(mmu->IoPendingValidPtr())));
        EmitTestRegReg(cursor, kEax, kEax);
        uint8_t* io_skip = EmitJnzLabel32(cursor);
        EmitWritebackRecomputed(cursor, d, ctx);
        FixupLabel32(io_skip, cursor);
    }
    if (align_done_label != nullptr) {
        FixupLabel32(align_done_label, cursor);
    }
    cursor = EmitAbortDataTail(cursor, d, ctx);

    /* .done: */
    for (int i = 0; i < n_done; ++i) {
        FixupLabel32(done_labels[i], cursor);
    }
    return cursor;
}
