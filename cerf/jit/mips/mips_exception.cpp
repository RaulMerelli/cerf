#include "mips_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../core/log.h"
#include "mips_cpu_state.h"
#include "mips_mmu.h"

/* CP0 synchronous-exception delivery, reimplemented from QEMU target/mips;
   per-function citations below. */

void MipsJit::EnterException(uint32_t cause, bool refill_eligible) {
    MipsCpuState& s = cpu_state_;

    /* Status.EXL is sampled once: it gates BOTH the refill-offset choice
       (do_interrupt EXCP_TLBL/TLBS line 1182) and the set_EPC block (line 1302),
       which both read the pre-exception value. */
    const bool exl = ((s.cp0_status >> MipsStatusBit::kEXL) & 1u) != 0u;

    uint32_t offset = MipsExcVector::kOffGeneral;            /* default 0x180 */
    if (refill_eligible && !exl) {
        offset = MipsExcVector::kOffRefill;                  /* TLB refill 0x000 */
    }

    if (!exl) {
        /* exception_resume_pc (exception.c:29): EPC = faulting PC, or branch PC
           (pc-4) in a delay slot. branch_state != kNone IS the delay-slot test -
           the branch place fn sets it before the slot, the resolve clears it only
           after. */
        const bool in_bds = (s.branch_state != MipsBranch::kNone);
        s.cp0_epc = in_bds ? (s.pc - 4u) : s.pc;
        if (in_bds) {
            s.cp0_cause |= (1u << MipsCauseBit::kBD);
        } else {
            s.cp0_cause &= ~(1u << MipsCauseBit::kBD);
        }
        s.cp0_status |= (1u << MipsStatusBit::kEXL);
    }

    /* Vector base: BEV picks the boot ROM space, else the fixed EBase. */
    const uint32_t base = ((s.cp0_status >> MipsStatusBit::kBEV) & 1u)
                              ? MipsExcVector::kBaseBev      /* 0xBFC00200 */
                              : MipsExcVector::kBaseNormal;  /* 0x80000000 */
    s.pc = base + offset;

    /* Cause.ExcCode (bits 6:2) is set even on a nested (EXL-already-set) take. */
    s.cp0_cause = (s.cp0_cause & ~(0x1Fu << MipsCauseBit::kExcCode)) |
                  (cause << MipsCauseBit::kExcCode);

    /* do_interrupt clears the branch-delay state unconditionally (line 1323). */
    s.branch_state = MipsBranch::kNone;
}

void MipsJit::SetMmuFaultRegs(uint32_t va) {
    /* raise_mmu_exception CP0 setup (tlb_helper.c:558-566), on min-page shift S:
       EntryHi/Context VPN2 = VA[31:S+1]. Context BadVPN2 field = [4+(31-S)-1 : 4]
       (VR4102 UM Fig 6-1: S=10 -> [24:4], VA>>7; R4000 S=12 -> [22:4], VA>>9). */
    MipsCpuState& s = cpu_state_;
    const uint32_t shift = s.min_page_shift;
    const uint32_t ctx_field = ((1u << (31u - shift)) - 1u) << 4;
    s.cp0_badvaddr = va;
    s.cp0_context  = (s.cp0_context & ~(ctx_field | 0xFu)) |
                     ((va >> (shift - 3u)) & ctx_field);
    s.cp0_entryhi  = (s.cp0_entryhi & 0xFFu) | (va & MipsVpn2Mask(shift));
}

void MipsJit::RaiseTlbException(uint32_t va, MipsAccess acc, MipsTlbResult res) {
    SetMmuFaultRegs(va);

    uint32_t cause = 0;
    bool nomatch = false;
    switch (res) {
        case MipsTlbResult::kNoMatch:   /* TLBRET_NOMATCH: TLBL/TLBS + refill vector */
            cause = (acc == MipsAccess::kWrite) ? MipsExcCode::kTLBS : MipsExcCode::kTLBL;
            nomatch = true;
            break;
        case MipsTlbResult::kInvalid:   /* TLBRET_INVALID: TLBL/TLBS, general vector */
            cause = (acc == MipsAccess::kWrite) ? MipsExcCode::kTLBS : MipsExcCode::kTLBL;
            break;
        case MipsTlbResult::kModified:  /* TLBRET_DIRTY: EXCP_LTLBL (TLB Modified) */
            cause = MipsExcCode::kMod;
            break;
        case MipsTlbResult::kMatch:     /* not a fault - corrupt call path */
            LOG(Caution, "MipsJit::RaiseTlbException: kMatch is not a fault (va=0x%08X)\n", va);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            return;
    }

    EnterException(cause, nomatch);
    RaiseException(kGuestExceptionCode, 0, 0, nullptr);
}

void MipsJit::DeliverFetchTlbException(uint32_t va, MipsTlbResult res) {
    /* Instruction-fetch TLB miss/invalid (raise_mmu_exception MMU_INST_FETCH ->
       EXCP_TLBL; refill vector when NOMATCH). NO SEH here - JitCompile runs OUTSIDE
       Run()'s __try; it sets CP0+vector and returns null so Run re-dispatches. */
    SetMmuFaultRegs(va);
    EnterException(MipsExcCode::kTLBL, /*refill_eligible=*/res == MipsTlbResult::kNoMatch);
}

void MipsJit::DeliverFetchAddressError(uint32_t va) {
    /* Unaligned instruction-fetch AdEL (QEMU mips_cpu_do_unaligned_access
       MMU_INST_FETCH -> EXCP_AdEL, op_helper.c:311-323; cause 4, general vector).
       No SEH: JitCompile runs outside Run()'s __try. */
    cpu_state_.cp0_badvaddr = va;
    EnterException(MipsExcCode::kAdEL, /*refill_eligible=*/false);
}

void MipsJit::RaiseAddressError(uint32_t va, MipsAccess acc) {
    /* mips_cpu_do_unaligned_access (op_helper.c:303): BadVAddr only (no Context /
       EntryHi), AdES for a store else AdEL. General vector. */
    cpu_state_.cp0_badvaddr = va;
    const uint32_t cause = (acc == MipsAccess::kWrite) ? MipsExcCode::kAdES
                                                       : MipsExcCode::kAdEL;
    EnterException(cause, false);
    RaiseException(kGuestExceptionCode, 0, 0, nullptr);
}

void MipsJit::RaiseOverflowException() {
    /* Integer Overflow (ADD/ADDI/SUB), cause 12, no address registers. The
       faulting PC is already live in cpu_state_.pc (per-insn materialization). */
    EnterException(MipsExcCode::kOv, false);
    RaiseException(kGuestExceptionCode, 0, 0, nullptr);
}

void __fastcall MipsJit::SyscallHelper(MipsJit* jit) {
    /* SYSCALL -> Sys, cause 8, general vector (tlb_helper.c do_interrupt:1234). */
    jit->EnterException(MipsExcCode::kSys, false);
    RaiseException(kGuestExceptionCode, 0, 0, nullptr);
}

void __fastcall MipsJit::BreakHelper(MipsJit* jit) {
    /* BREAK -> Bp, cause 9, general vector (tlb_helper.c do_interrupt:1238). */
    jit->EnterException(MipsExcCode::kBp, false);
    RaiseException(kGuestExceptionCode, 0, 0, nullptr);
}

void __fastcall MipsJit::EretHelper(MipsJit* jit) {
    MipsCpuState& s = jit->cpu_state_;
    /* MIPS64 Vol2 ERET: ERL takes precedence over EXL. */
    if ((s.cp0_status >> MipsStatusBit::kERL) & 1u) {
        s.pc = s.cp0_errorepc;
        s.cp0_status &= ~(1u << MipsStatusBit::kERL);
    } else {
        s.pc = s.cp0_epc;
        s.cp0_status &= ~(1u << MipsStatusBit::kEXL);
    }
    s.llbit = 0;
}
