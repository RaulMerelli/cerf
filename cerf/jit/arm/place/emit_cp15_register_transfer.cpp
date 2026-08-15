#include <cstddef>
#include <cstdint>

#include "../../../cpu/arm_processor_config.h"
#include "../arm_emit_services.h"
#include "../arm_mmu.h"
#include "../arm_translation_cache.h"
#include "../arm_mmu_state.h"
#include "../arm_tlb_ops.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {

/* Store Rd into the cp15 field at mmu_disp; on a real change drop the VA-keyed
   native caches via ContextSwitchFlush. NOT a translation-cache flush - blocks
   are phys-keyed so they survive an address-space change; a TC flush here would
   reinstate the per-context-switch storm. `mask` is ANDed in (0xFFFFFFFF=none). */
uint8_t* EmitFieldWriteContextSwitch(uint8_t* cursor, ArmTranslationCache* tc,
                                     int32_t rd_disp, int32_t mmu_disp,
                                     uint32_t mask) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
    if (mask != 0xFFFFFFFFu) EmitAndRegImm32(cursor, kEax, mask);
    EmitCmpRegBaseDisp32(cursor, kEax, kMmuReg, mmu_disp);
    EmitMovBaseDisp32Reg(cursor, kMmuReg, mmu_disp, kEax);   /* store (keeps flags) */
    uint8_t* same = EmitJzLabel(cursor);                     /* unchanged → no flush */
    EmitMovRegImm32(cursor, kEcx,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tc)));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmTranslationCache::ContextSwitchFlushHelper));
    FixupLabel(same, cursor);
    return cursor;
}

}  // namespace

/* Shared cp15 MRC / MCR emit body - CRn dispatch common across the
   ARMv4T..v7 cores supported today. Per-CPU concretes intercept any
   register whose semantics diverge before delegating here. */
uint8_t* EmitCp15RegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const int32_t rd_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);

    switch (d->crn) {
    case 0:
        if (emit->ProcessorConfig()->HasCp15V7() && d->cp_opc == 1 &&
            d->crm == 0 && d->l) {
            /* MRC p15, 1, Rt, c0, c0, {0,1}. op2=0 → CCSIDR (depends
               on current CSSELR, dispatch through ArmMmu helper);
               op2=1 → CLIDR (constant baked from ProcessorConfig). */
            if (d->cp == 0) {
                /* CcsidrLookupHelper __fastcall(ArmMmu*) - ECX = the
                   ArmMmu service pointer, NOT kMmuReg (which holds
                   ArmMmuState* and would make mmu->emu_ read garbage). */
                EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(emit->Mmu())));
                EmitCall(cursor,
                    reinterpret_cast<void*>(&ArmMmu::CcsidrLookupHelper));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else if (d->cp == 1) {
                EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                    emit->ProcessorConfig()->Clidr());
            } else {
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            }
        } else if (emit->ProcessorConfig()->HasCp15V7() && d->cp_opc == 2 &&
                   d->crm == 0 && d->cp == 0) {
            /* MRC/MCR p15, 2, Rt, c0, c0, 0 - CSSELR R/W. Per-CPU
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
        } else if (d->cp_opc == 0 &&
                   (d->crm == 0 || !emit->ProcessorConfig()->HasCp15V6())) {
            /* Legacy MIDR/CTR path - read-only constants from
               ArmProcessorConfig. */
            if (d->l) {
                if (d->cp == 0) {
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                        emit->ProcessorConfig()->Midr());
                } else if (d->cp == 1) {
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                        emit->ProcessorConfig()->Ctr());
                } else {
                    cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
                }
            } else {
                /* ARM DDI 0406C.c p. B3-1449: an MCR access to an RO register
                   is UNPREDICTABLE (identification registers are RO); glossary:
                   an UNPREDICTABLE instruction can be implemented as UNDEFINED. */
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            }
        } else {
            /* Anything not covered above (e.g. op1=3/4/5/6/7 reads,
               or a v7-only op1 on a pre-v7 chip) raises UND. */
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        break;

    case 1: {
        /* ARM DDI 0406C.c Figure B3-28 (p. B3-1472): c1 op1=0 CRm=c0 op2=0
           SCTLR, op2=1 ACTLR, op2=2 CPACR. p. B3-1472: encodings not shown,
           and encodings that are part of an unimplemented architectural
           extension, are UNPREDICTABLE -> UND (glossary). op1=0 CRm=c1 is
           the Security Extensions bank (Fig B3-28: SCR/SDER/NSACR at
           op2=0..2), implemented silicon on Cortex-A8 (ARM DDI 0344 §2.1)
           that CERF does not model - every op2 there fatals. */
        if (d->cp_opc == 0 && d->crm == 1 &&
            emit->ProcessorConfig()->HasSecurityExtensions()) {
            cursor = EmitCoprocUnimplementedFatal(cursor, d, ctx);
            break;
        }
        /* ARM DDI 0100I B4.9 (p. B4-39): "Unless specified otherwise, CRm and
           opcode_2 SBZ." */
        if (d->cp_opc != 0 ||
            (d->crm != 0 && emit->ProcessorConfig()->HasCp15V6())) {
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            break;
        }
        const uint32_t c1_op2 =
            (emit->ProcessorConfig()->HasAuxControlRegister() ||
             emit->ProcessorConfig()->HasCp15V6()) ? d->cp : 0u;
        if (d->l) {
            if (c1_op2 == 0) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, control_register)));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            /* ACTLR only where the core allocates c1 op2=1; an unallocated
               encoding in an allocated primary register is UNPREDICTABLE
               (B3.15 rule 2, p. B3-1447) -> UND (glossary). */
            } else if (c1_op2 == 1 &&
                       emit->ProcessorConfig()->HasAuxControlRegister()) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, aux_control_register)));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else if (c1_op2 == 2 && emit->ProcessorConfig()->HasCp15V6()) {
                EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)));
                EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            } else {
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
                break;
            }
        } else {
            if (c1_op2 == 0) {
                EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
                EmitCall(cursor, ctx->sctlr_write_target);
            } else if (c1_op2 == 1 &&
                       emit->ProcessorConfig()->HasAuxControlRegister()) {
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitMovBaseDisp32Reg(cursor, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, aux_control_register)), kEax);
            } else if (c1_op2 == 2 && emit->ProcessorConfig()->HasCp15V6()) {
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitMovBaseDisp32Reg(cursor, kMmuReg,
                    static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)), kEax);
            } else {
                cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            }
        }
        break;
    }

    case 2: {
        const int32_t ttbr0_disp =
            static_cast<int32_t>(offsetof(ArmMmuState, translation_table_base));
        if (emit->ProcessorConfig()->HasCp15V6() &&
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
        /* ARM DDI 0406C.c B4.1.154: TTBR0 holds the table base at bits[31:x]
           (x = 14-N); bits[x-1:6] are Reserved UNK/SBZP and bits[5:0] are
           cacheability/shareability attributes, so a write of any value is
           architecturally permitted and the walker masks the base on use.
           CONTEXTIDR/TTBR0 identify the address space (B3.9.1), so a write
           is a context switch. */
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, ttbr0_disp);
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            cursor = EmitFieldWriteContextSwitch(cursor, emit->TranslationCache(), rd_disp, ttbr0_disp,
                                                 0xFFFFFFFFu);
        }
        break;
    }

    case 3: {
        /* DACR is a plain 32-bit RW register (ARM DDI 0406C B4.1.43); the
           walker enforces the per-domain fields at translation time. */
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, domain_access_control)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitMovBaseDisp32Reg(cursor, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, domain_access_control)), kEax);
            /* Flush both TLBs: a DACR change alters live AP enforcement, but the
               inline fast path trusts the install-time permission, so a stale
               entry would keep using the old domain access. */
            EmitLeaRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, data_tlb)));
            EmitPushReg(cursor, kEax);
            EmitCall(cursor, reinterpret_cast<void*>(&ArmTlbFlushAll));
            EmitAddRegImm32(cursor, kEsp, 4);
            EmitLeaRegBaseDisp32(cursor, kEax, kMmuReg,
                static_cast<int32_t>(offsetof(ArmMmuState, instruction_tlb)));
            EmitPushReg(cursor, kEax);
            EmitCall(cursor, reinterpret_cast<void*>(&ArmTlbFlushAll));
            EmitAddRegImm32(cursor, kEsp, 4);
        }
        break;
    }

    case 4:
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;

    case 5: {
        /* ARM DDI 0406C.c Figure B3-31 (p. B3-1474): c5 op1=0 CRm=c0 op2=0
           DFSR, op2=1 IFSR (VMSAv6+, D12.6); encodings not shown are
           UNPREDICTABLE, implemented as UNDEFINED (glossary). */
        const bool is_ifsr = d->cp == 1 && emit->ProcessorConfig()->HasCp15V6();
        if (d->cp_opc != 0 ||
            (d->crm != 0 && emit->ProcessorConfig()->HasCp15V6()) ||
            (d->cp != 0 && !is_ifsr &&
             emit->ProcessorConfig()->HasCp15V6())) {
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            break;
        }
        const int32_t disp = is_ifsr
            ? static_cast<int32_t>(offsetof(ArmMmuState, ifsr))
            : static_cast<int32_t>(offsetof(ArmMmuState, fault_status));
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, disp);
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitMovBaseDisp32Reg(cursor, kMmuReg, disp, kEax);
        }
        break;
    }

    case 6: {
        /* ARM DDI 0406C.c Figure B3-31 (p. B3-1474): c6 op1=0 CRm=c0 op2=0
           DFAR, op2=2 IFAR (VMSAv6+, D12.6); encodings not shown are
           UNPREDICTABLE, implemented as UNDEFINED (glossary). */
        const bool is_ifar = d->cp == 2 && emit->ProcessorConfig()->HasCp15V6();
        if (d->cp_opc != 0 ||
            (d->crm != 0 && emit->ProcessorConfig()->HasCp15V6()) ||
            (d->cp != 0 && !is_ifar &&
             emit->ProcessorConfig()->HasCp15V6())) {
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            break;
        }
        const int32_t disp = is_ifar
            ? static_cast<int32_t>(offsetof(ArmMmuState, ifar))
            : static_cast<int32_t>(offsetof(ArmMmuState, fault_address));
        if (d->l) {
            EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, disp);
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
            EmitMovBaseDisp32Reg(cursor, kMmuReg, disp, kEax);
        }
        break;
    }

    case 7:
        cursor = EmitCp15CacheOp(cursor, d, ctx);
        break;

    case 8:
        cursor = EmitCp15TlbOp(cursor, d, ctx);
        break;

    case 9:
        /* c9,c0,2 op1=1: L2 Cache Auxiliary Control Register (Cortex-A8, ARM
           DDI0344K §3.2.55). CERF models no L2 → config latch (read/write field).
           HW restricts the write to Secure state; CERF has no NS world and the
           guest boots Secure, so the write is always taken. */
        if (emit->ProcessorConfig()->HasL2CacheAuxControl() && d->cp_opc == 1 &&
            d->crm == 0 && d->cp == 2) {
            const int32_t disp =
                static_cast<int32_t>(offsetof(ArmMmuState, l2_aux_control));
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

    case 10: {
        /* PRRR/NMRR storage only valid while SCTLR.TRE=0 - if TRE
           becomes 1, walker must consult these or attributes diverge. */
        if (emit->ProcessorConfig()->HasCp15V6() && d->cp_opc == 0 &&
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
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;

    case 12:
        /* ARM DDI 0406C.c Figure B3-38 (p. B3-1479): with the Security
           Extensions c12 = VBAR / MVBAR / ISR - implemented silicon on
           Cortex-A8 (ARM DDI 0344 §2.1) that CERF does not model; without
           them "all CP15 c12 encodings are UNDEFINED" (p. B3-1479). */
        if (emit->ProcessorConfig()->HasSecurityExtensions()) {
            cursor = EmitCoprocUnimplementedFatal(cursor, d, ctx);
        } else {
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        break;

    case 13: {
        if (emit->ProcessorConfig()->HasCp15V6() &&
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
            } else if (d->cp == 1) {
                /* CONTEXTIDR[7:0] is the ASID - an address-space switch. */
                cursor = EmitFieldWriteContextSwitch(cursor, emit->TranslationCache(), rd_disp, disp,
                                                     0xFFFFFFFFu);
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
            /* FCSE PID = bits[31:25] (ARM1136 TRM §3.3.35); [24:0] SBZ, ignored
               not faulted. Mask so the walker's `p |= process_id` fold is right.
               PID reuse is the stale-block trigger → context-switch flush. */
            cursor = EmitFieldWriteContextSwitch(cursor, emit->TranslationCache(), rd_disp,
                static_cast<int32_t>(offsetof(ArmMmuState, process_id)),
                0xFE000000u);
        }
        break;
    }

    case 14:
        /* ARM DDI 0406C.c p. B3-1447 rule 1: accesses to unallocated CP15
           primary registers are UNDEFINED; c14 is unallocated on an
           implementation without the Generic Timer Extension. */
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;

    case 15:
        /* ARM DDI 0344K Table 3-3 (p. 3-17): Cortex-A8 c15 op1=0 CRm=c2 opc2=4
           is the D-TLB PA read operation, write-only; its result appears in the
           D-L1 Data 0 Register (Table 3-150, p. 3-126). */
        if (emit->ProcessorConfig()->HasL1SystemArrayDebug() && !d->l &&
            d->cp_opc == 0 && d->crm == 2 && d->cp == 4) {
            break;
        }
        cursor = EmitCoprocUnimplementedFatal(cursor, d, ctx);
        break;
    }

    return cursor;
}
