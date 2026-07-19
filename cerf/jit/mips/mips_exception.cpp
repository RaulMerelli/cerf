#include "mips_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../core/log.h"
#include "../../cpu/mips_processor_config.h"
#include "mips_cpu_state.h"
#include "mips_exception_model.h"
#include "mips_mmu.h"

/* CP0 synchronous-exception delivery, reimplemented from QEMU target/mips;
   per-function citations below. */

void MipsJit::EnterException(uint32_t cause, bool refill_eligible) {
    exception_->Enter(this, cause, refill_eligible);
}

void MipsJit::SetMmuFaultRegs(uint32_t va) {
    exception_->SetMmuFaultRegs(this, va);
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

void __fastcall MipsJit::RfeHelper(MipsJit* jit) {
    MipsCpuState& s = jit->cpu_state_;
    /* IEc<-IEp, KUc<-KUp, IEp<-IEo, KUp<-KUo; IEo and KUo retain their values
       (TMPR39xx-um Fig 6-7). Status bits: IEc<0> KUc<1> IEp<2> KUp<3> IEo<4>
       KUo<5> (TMPR39xx-um §6.2.3). The Cache register's auto-lock half of RFE
       moves bits the TX39 CP0 register file does not surface. */
    s.cp0_status = (s.cp0_status & ~0xFu) | ((s.cp0_status >> 2) & 0xFu);
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
    /* "The ERET instruction loads the ISA mode from bit 0 of the EPC or
       ErrorEPC register" when MIPS16 is enabled (U15509EJ2V0UM 3.4.3). */
    if (jit->CpuConfig()->HasMips16()) {
        s.isa_mode = s.pc & 1u;
        s.pc &= ~1u;
    }
    s.llbit = 0;
}
