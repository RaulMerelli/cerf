#include <cstddef>
#include <cstdint>

#include "../../cpu/arm_processor_config.h"
#include "../arm_jit.h"
#include "../arm_mmu.h"
#include "../arm_mmu_state.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

/* Shared cp15 (system control coprocessor) MRC / MCR emit body —
   the CRn dispatch shape is common to every ARM920T-class core
   regardless of SoC, so this lives at the JIT-place layer and per-SoC
   CoprocEmitter concretes delegate here for cp_num == 15. */
uint8_t* EmitCp15RegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;

    const int32_t rd_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);

    switch (d->crn) {
    case 0:
        if (jit->ProcessorConfig()->HasCp15V7() && d->cp_opc == 1 &&
            d->crm == 0 && d->l) {
            /* MRC p15, 1, Rt, c0, c0, {0,1}. op2=0 → CCSIDR (depends
               on current CSSELR, dispatch through ArmMmu helper);
               op2=1 → CLIDR (constant baked from ProcessorConfig). */
            if (d->cp == 0) {
                /* CcsidrLookupHelper __fastcall(ArmMmu*) — ECX = the
                   ArmMmu service pointer, NOT kMmuReg (which holds
                   ArmMmuState* and would make mmu->emu_ read garbage). */
                EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(jit->Mmu())));
                EmitCall(cursor,
                    reinterpret_cast<void*>(&ArmMmu::CcsidrLookupHelper));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else if (d->cp == 1) {
                EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                    jit->ProcessorConfig()->Clidr());
            } else {
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            }
        } else if (jit->ProcessorConfig()->HasCp15V7() && d->cp_opc == 2 &&
                   d->crm == 0 && d->cp == 0) {
            /* MRC/MCR p15, 2, Rt, c0, c0, 0 — CSSELR R/W. Per-CPU
               mutable state stored in ArmMmuState::cssel_register.
               Source: QEMU helper.c:948-955 (PL1_RW, .resetvalue=0,
               banked storage). */
            const int32_t csselr_disp =
                static_cast<int32_t>(offsetof(ArmMmuState, cssel_register));
            if (d->l) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, csselr_disp);
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else {
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitMovBaseDisp32Reg(cursor, kMmuReg, csselr_disp, kEax);
            }
        } else if (d->cp_opc == 0 && d->crm == 0) {
            /* Legacy MIDR/CTR path. Both read-only constants pulled
               from ArmProcessorConfig so the per-SoC strategy owns
               them rather than the JIT body. */
            if (d->l) {
                if (d->cp == 0) {
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                        jit->ProcessorConfig()->Midr());
                } else if (d->cp == 1) {
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                        jit->ProcessorConfig()->Ctr());
                } else {
                    cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
                }
            } else {
                /* Writes to op1=0/CP=0/CP=1 are silently ignored on
                   real hardware; CP>1 UND-faults. */
                if (d->cp > 1) {
                    cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
                }
            }
        } else {
            /* Anything not covered above (e.g. op1=3/4/5/6/7 reads,
               or a v7-only op1 on a pre-v7 chip) raises UND. */
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        break;

    case 1:
        if (d->l) {
            if (d->cp == 0) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, control_register)));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else if (d->cp == 1) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, aux_control_register)));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else if (d->cp == 2 && jit->ProcessorConfig()->HasCp15V7() &&
                       d->cp_opc == 0 && d->crm == 0) {
                /* v7 CPACR — same coprocessor-access concept as the
                   ARM920T CRn=15 register, just relocated. Reuse
                   coprocessor_access storage. */
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else {
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
                break;
            }
        } else {
            if (d->cp == 0) {
                EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
                EmitMovRegImm32(cursor, kEdx, d->guest_address);
                EmitCall(cursor, ctx->sctlr_write_target);
            } else if (d->cp == 1) {
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                /* Reserved bits must be 0. */
                EmitTestRegImm32(cursor, kEax, 0xFFFFFFCCu);
                uint8_t* raise_label = EmitJnzLabel(cursor);
                /* P bit (page table memory type) must be set. */
                EmitTestRegImm32(cursor, kEax, 2u);
                uint8_t* store_label = EmitJnzLabel(cursor);
                FixupLabel(raise_label, cursor);
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
                FixupLabel(store_label, cursor);
                EmitMovBaseDisp32Reg(cursor, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, aux_control_register)), kEax);
            } else if (d->cp == 2 && jit->ProcessorConfig()->HasCp15V7() &&
                       d->cp_opc == 0 && d->crm == 0) {
                /* v7 CPACR write — accept any value into the same
                   coprocessor_access slot. The walker doesn't gate
                   on CPACR yet; storage-only is consistent with the
                   CRn=15 ARM920T path. */
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitMovBaseDisp32Reg(cursor, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)), kEax);
            } else {
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            }
        }
        break;

    case 2: {
        /* Register 2 — Translation Table Base 0 / 1 / control. v7
           cores add TTBR1 (op2=1) and TTBCR (op2=2) at the same
           CRn. See references/omap3530/armv7_arch_excerpts.txt
           § cp15 c2/c10/c13 for QEMU encodings. */
        if (jit->ProcessorConfig()->HasCp15V7() &&
            (d->cp == 1 || d->cp == 2)) {
            const int32_t disp = (d->cp == 1)
                ? static_cast<int32_t>(offsetof(ArmMmuState, ttbr1))
                : static_cast<int32_t>(offsetof(ArmMmuState, ttbcr));
            if (d->l) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, disp);
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else {
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitMovBaseDisp32Reg(cursor, kMmuReg, disp, kEax);
            }
            break;
        }
        /* v7: TTBR0 writes accept any value; the walker masks to bits[31:14]
           (armv7_arch_excerpts.txt:522-527) on use. Re-tightening here UND-
           faults legitimate kernel sentinel writes (observed: R1=0xFFFFFFFF). */
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, translation_table_base)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else if (jit->ProcessorConfig()->HasCp15V7()) {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, translation_table_base)), kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
            EmitMovRegReg(cursor, kEax, kEcx);
            EmitAndRegImm32(cursor, kEax, 0x00003FFFu);
            uint8_t* raise_label = EmitJnzLabel(cursor);
            EmitMovRegImm32(cursor, kEdx,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(jit)));
            EmitCall(cursor,
                reinterpret_cast<void*>(&ArmJit::MapGuestPhysicalToHostRamHelper));
            EmitTestRegReg(cursor, kEax, kEax);
            uint8_t* store_label = EmitJnzLabel(cursor);
            FixupLabel(raise_label, cursor);
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            FixupLabel(store_label, cursor);
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, translation_table_base)), kEax);
        }
        break;
    }

    case 3: {
        /* DACR=1 only — the MMU walker skips per-domain checks; any
           other value would silently bypass that assumption. */
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, domain_access_control)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitCmpRegImm32(cursor, kEax, 1);
            uint8_t* store_label = EmitJzLabel(cursor);
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            FixupLabel(store_label, cursor);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, domain_access_control)), kEax);
        }
        break;
    }

    case 4:
        /* Register 4 — reserved on ARM920T. */
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;

    case 5:
        /* Register 5 — Fault Status Register. Direct read/write. */
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, fault_status)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, fault_status)), kEax);
        }
        break;

    case 6:
        /* Register 6 — Fault Address Register. Direct read/write. */
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, fault_address)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, fault_address)), kEax);
        }
        break;

    case 7:
        cursor = EmitCp15CacheOp(cursor, d, ctx);
        break;

    case 8:
        cursor = EmitCp15TlbOp(cursor, d, ctx);
        break;

    case 9:
        /* Cache / TLB lockdown — not modeled. */
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;

    case 10: {
        /* PRRR/NMRR storage only valid while SCTLR.TRE=0 — if TRE
           becomes 1, walker must consult these or attributes diverge. */
        if (jit->ProcessorConfig()->HasCp15V7() && d->cp_opc == 0 &&
            d->crm == 2 && (d->cp == 0 || d->cp == 1)) {
            const int32_t disp = (d->cp == 0)
                ? static_cast<int32_t>(offsetof(ArmMmuState, prrr))
                : static_cast<int32_t>(offsetof(ArmMmuState, nmrr));
            if (d->l) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, disp);
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else {
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitMovBaseDisp32Reg(cursor, kMmuReg, disp, kEax);
            }
        } else {
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        break;
    }

    case 11:
    case 12:
        /* Reserved on ARM920T. */
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;

    case 13: {
        if (jit->ProcessorConfig()->HasCp15V7() &&
            d->cp >= 1 && d->cp <= 4) {
            int32_t disp = 0;
            switch (d->cp) {
            case 1: disp = static_cast<int32_t>(offsetof(ArmMmuState, contextidr)); break;
            case 2: disp = static_cast<int32_t>(offsetof(ArmMmuState, tpidrurw));   break;
            case 3: disp = static_cast<int32_t>(offsetof(ArmMmuState, tpidruro));   break;
            case 4: disp = static_cast<int32_t>(offsetof(ArmMmuState, tpidrprw));   break;
            }
            if (d->l) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, disp);
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else {
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitMovBaseDisp32Reg(cursor, kMmuReg, disp, kEax);
            }
            break;
        }
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, process_id)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitTestRegImm32(cursor, kEax, 0x01FFFFFFu);
            uint8_t* store_label = EmitJzLabel(cursor);
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            FixupLabel(store_label, cursor);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, process_id)), kEax);
        }
        break;
    }

    case 14:
        /* Breakpoint registers — not modeled. */
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;

    case 15: {
        /* Register 15 — Coprocessor Access Control. Read direct;
           write must leave bits[31:14] clear (only the per-cp-num
           access bits[13:0] are meaningful). */
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitTestRegImm32(cursor, kEax, 0xFFFFC000u);
            uint8_t* store_label = EmitJzLabel(cursor);
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            FixupLabel(store_label, cursor);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)), kEax);
        }
        break;
    }

    default:
        /* CRn is a 4-bit field so always 0..15 — this branch is
           unreachable; matches the reference's ASSERT(FALSE) +
           defensive UND raise. */
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;
    }

    return cursor;
}
