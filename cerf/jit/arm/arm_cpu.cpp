#include "arm_cpu.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../cpu/arm_processor_config.h"
#include "../guest_engine.h"
#include "arm_mmu.h"
#include "arm_tlb_ops.h"
#include "../../state/state_stream.h"

REGISTER_SERVICE(ArmCpu);

namespace {

/* ARM ARM DDI 0406C.c B1.3.2, p. B1-1145: RBankSelect() '11111' System selects
   the usr bank. Monitor and Hyp are extension-only (Table B1-1, p. B1-1139) and
   every other encoding is reserved, where the same page's accessors specify
   "if BadMode(mode) then UNPREDICTABLE". */
ArmBank SelectBank(uint32_t mode) {
    switch (mode) {
    case ArmMode::kUser:
    case ArmMode::kSystem:     return ArmBank::kUsr;
    case ArmMode::kFiq:        return ArmBank::kFiq;
    case ArmMode::kIrq:        return ArmBank::kIrq;
    case ArmMode::kSupervisor: return ArmBank::kSvc;
    case ArmMode::kAbort:      return ArmBank::kAbt;
    case ArmMode::kUndefined:  return ArmBank::kUnd;
    default:
        LOG(Caution, "ArmCpu: bank for CPSR.M=0x%02X unmodelled\n", mode);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1152: SPSR[] is "otherwise UNPREDICTABLE"
   outside the seven modes it enumerates, so User and System have none. */
ArmSpsrBank SelectSpsrBank(uint32_t mode) {
    switch (mode) {
    case ArmMode::kFiq:        return ArmSpsrBank::kFiq;
    case ArmMode::kIrq:        return ArmSpsrBank::kIrq;
    case ArmMode::kSupervisor: return ArmSpsrBank::kSvc;
    case ArmMode::kAbort:      return ArmSpsrBank::kAbt;
    case ArmMode::kUndefined:  return ArmSpsrBank::kUnd;
    default:
        LOG(Caution, "ArmCpu: SPSR access in CPSR.M=0x%02X is UNPREDICTABLE\n", mode);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

constexpr uint32_t kBankIdx(ArmBank b)     { return static_cast<uint32_t>(b); }
constexpr uint32_t kSpsrIdx(ArmSpsrBank b) { return static_cast<uint32_t>(b); }

}  /* namespace */

bool ArmCpu::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

/* ARM ARM DDI 0406C.c B1.3.2, p. B1-1143: the application-level registers are
   "selected from a larger set of registers, that includes Banked copies of some
   registers, with the current register selected by the execution mode". */
void ArmCpu::SwitchModeBanks(uint32_t old_mode, uint32_t new_mode) {
    if (old_mode == new_mode) {
        return;
    }

    const ArmBank old_bank = SelectBank(old_mode);
    const ArmBank new_bank = SelectBank(new_mode);
    if (old_bank == new_bank) {
        return;
    }

    state_.sp_bank[kBankIdx(old_bank)] = state_.gprs[ArmGpr::kR13];
    state_.gprs[ArmGpr::kR13]          = state_.sp_bank[kBankIdx(new_bank)];

    state_.lr_bank[kBankIdx(old_bank)] = state_.gprs[ArmGpr::kR14];
    state_.gprs[ArmGpr::kR14]          = state_.lr_bank[kBankIdx(new_bank)];

    /* ARM ARM DDI 0406C.c Figure B1-2, p. B1-1144: only FIQ banks R8-R12. */
    const bool old_fiq = (old_mode == ArmMode::kFiq);
    const bool new_fiq = (new_mode == ArmMode::kFiq);
    if (old_fiq != new_fiq) {
        for (uint32_t i = 0; i < 5u; ++i) {
            const uint32_t live = state_.gprs[8u + i];
            state_.gprs[8u + i]   = state_.r8_r12_fiq[i];
            state_.r8_r12_fiq[i]  = live;
        }
    }
}

uint32_t* ArmCpu::BankedSp(uint32_t mode) {
    const ArmBank bank = SelectBank(mode);
    if (bank == SelectBank(state_.cpsr.bits.mode)) {
        return &state_.gprs[ArmGpr::kR13];
    }
    return &state_.sp_bank[kBankIdx(bank)];
}

ArmPsrFull* ArmCpu::BankedSpsr(uint32_t mode) {
    return &state_.spsr_bank[kSpsrIdx(SelectSpsrBank(mode))];
}

void ArmCpu::UpdateCpsrWithFlags(ArmPsrFull psr) {
    if (psr.bits.thumb_mode && !emu_.Get<ArmProcessorConfig>().HasThumb()) {
        LOG(Caution, "ArmCpu: CPSR write sets T on a core whose "
                     "ArmProcessorConfig reports no Thumb state\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t old_mode = state_.cpsr.bits.mode;
    const uint32_t old_irq  = state_.cpsr.bits.irq_disable;

    state_.cpsr = psr;
    SwitchModeBanks(old_mode, state_.cpsr.bits.mode);

    if (old_irq != state_.cpsr.bits.irq_disable) {
        emu_.Get<GuestEngine>().ResyncInterruptPoll();
    }
}

/* ARM ARM DDI 0406C.c B1.9, pp. B1-1206 (Undefined Instruction), B1-1210 (SVC),
   B1-1213 (Prefetch Abort), B1-1215 (Data Abort), B1-1219 (IRQ): SPSR[] = old
   CPSR, R[14] = the exception's return value, CPSR.M = its mode, CPSR.I = '1',
   CPSR.IT = '00000000', CPSR.J = '0'. */
void ArmCpu::EnterException(uint32_t target_mode,
                            uint32_t new_lr_value,
                            uint32_t vect_offset,
                            bool     set_async_abort_mask) {
    const ArmPsrFull old_cpsr = state_.cpsr;
    const uint32_t   old_mode = old_cpsr.bits.mode;

    state_.cpsr.bits.mode = target_mode;
    SwitchModeBanks(old_mode, target_mode);

    state_.spsr_bank[kSpsrIdx(SelectSpsrBank(target_mode))] = old_cpsr;
    state_.gprs[ArmGpr::kR14] = new_lr_value;

    const uint32_t old_irq       = old_cpsr.bits.irq_disable;
    state_.cpsr.bits.irq_disable = 1u;
    if (old_irq != 1u) {
        emu_.Get<GuestEngine>().ResyncInterruptPoll();
    }
    state_.cpsr.bits.it_low      = 0u;
    state_.cpsr.bits.it_high     = 0u;
    state_.cpsr.bits.jazelle     = 0u;
    if (set_async_abort_mask) {
        state_.cpsr.bits.async_abort_disable = 1u;
    }
    ApplySctlrExecutionState();
    state_.gprs[ArmGpr::kR15] = ExcVectorBase() + vect_offset;
}

void ArmCpu::RaiseUndefinedException(uint32_t guest_pc) {
    EnterException(ArmMode::kUndefined, ReturnAddress(guest_pc, 2u, 4u), 4u, false);
}

void ArmCpu::RaiseSwiException(uint32_t guest_pc) {
    EnterException(ArmMode::kSupervisor, ReturnAddress(guest_pc, 2u, 4u), 8u, false);
}

void ArmCpu::RaiseAbortPrefetchException(uint32_t guest_pc) {
    EnterException(ArmMode::kAbort, ReturnAddress(guest_pc, 0u, 4u), 12u, true);
}

void ArmCpu::RaiseIrqException(uint32_t guest_pc) {
    EnterException(ArmMode::kIrq, ReturnAddress(guest_pc, 0u, 4u), 24u, true);
}

/* ARM ARM DDI 0406C.c A2.3, p. A2-45: "When executing an ARM instruction, PC
   reads as the address of the current instruction plus 8"; Thumb reads plus 4. */
uint32_t ArmCpu::ReturnAddress(uint32_t guest_pc, int32_t thumb_sub, int32_t arm_sub) {
    return state_.cpsr.bits.thumb_mode
               ? static_cast<uint32_t>(static_cast<int32_t>(guest_pc + 4u) - thumb_sub)
               : static_cast<uint32_t>(static_cast<int32_t>(guest_pc + 8u) - arm_sub);
}

/* ARM ARM DDI 0406C.c B1.8.1 ExcVectorBase(): SCTLR.V=1 -> 0xFFFF0000
   (Hivecs); else HaveSecurityExt() -> VBAR, else 0. VBAR's reset value is
   IMPLEMENTATION DEFINED (B4.1.156) - CERF defines 0; cp15 c12 is
   unimplemented and fatals on a Security-Extensions core (the only cores
   that have VBAR), so VBAR never leaves its reset value. */
uint32_t ArmCpu::ExcVectorBase() {
    return emu_.Get<ArmMmu>().State()->control_register.bits.v ? 0xFFFF0000u
                                                               : 0u;
}

/* ARM ARM DDI 0406C.c B1.9, p. B1-1206 and B1.9.1 TakeReset(): "CPSR.J = '0';
   CPSR.T = SCTLR.TE" and "CPSR.E = SCTLR.EE". TE exists on ARMv6T2/ARMv7
   only, EE from ARMv6 (D12.7.4); ARMv4/v5 SCTLR bits[31:16] are Reserved
   UNK/SBZP (D15.7). */
void ArmCpu::ApplySctlrExecutionState() {
    const ArmSctlr sctlr = emu_.Get<ArmMmu>().State()->control_register;
    ArmProcessorConfig& config = emu_.Get<ArmProcessorConfig>();
    state_.cpsr.bits.thumb_mode = config.HasCp15V7() ? sctlr.bits.te : 0u;
    state_.cpsr.bits.endian     = config.HasCp15V6() ? sctlr.bits.ee : 0u;
}

void ArmCpu::RaiseAbortDataException(uint32_t guest_pc) {
    EnterException(ArmMode::kAbort, ReturnAddress(guest_pc, -4, 0), 16u, true);
}

/* ARM ARM DDI 0406C.c B1.9.1 TakeReset(): CPSR.M = '10011' Supervisor, then
   CPSR.I = '1'; CPSR.F = '1'; CPSR.A = '1'. */
void ArmCpu::RaiseResetException(uint32_t initial_pc) {
    initial_pc_ = initial_pc;

    const uint32_t old_mode = state_.cpsr.bits.mode;
    state_.cpsr.bits.mode   = ArmMode::kSupervisor;
    SwitchModeBanks(old_mode, ArmMode::kSupervisor);

    /* ARM ARM DDI 0406C.c B1.9.1 TakeReset(): "if HaveAdvSIMDorVFP() then
       FPEXC.EN = '0'", and "All registers, bits and fields not reset by the
       above pseudocode ... are UNKNOWN bitstrings after reset", so 0 is a
       permitted value for every other FPEXC bit. */
    state_.fpexc = 0u;

    const uint32_t old_irq               = state_.cpsr.bits.irq_disable;
    state_.cpsr.bits.irq_disable         = 1u;
    if (old_irq != 1u) {
        emu_.Get<GuestEngine>().ResyncInterruptPoll();
    }
    state_.cpsr.bits.fiq_disable         = 1u;
    state_.cpsr.bits.async_abort_disable = 1u;
    state_.cpsr.bits.it_low              = 0u;
    state_.cpsr.bits.it_high             = 0u;
    state_.cpsr.bits.jazelle             = 0u;

    if (pending_resume_mmu_set_) {
        ArmMmuState* mmu_state = emu_.Get<ArmMmu>().State();
        mmu_state->control_register.word       = pending_resume_control_;
        mmu_state->translation_table_base.word = pending_resume_ttbr0_;
        mmu_state->domain_access_control       = pending_resume_dacr_;
        ArmTlbFlushAll(&mmu_state->data_tlb);
        ArmTlbFlushAll(&mmu_state->instruction_tlb);
        pending_resume_mmu_set_ = false;
    }

    state_.deep_sleep    = 0u;
    state_.reset_pending = 0u;

    ApplySctlrExecutionState();

    if (pending_resume_pc_set_) {
        state_.gprs[ArmGpr::kR15] = pending_resume_pc_;
        pending_resume_pc_set_    = false;
    } else {
        state_.gprs[ArmGpr::kR15] = initial_pc;
    }
}

void ArmCpu::RaiseResetException() {
    RaiseResetException(initial_pc_);
}

void ArmCpu::SetInitialStackPointer(uint32_t sp) {
    state_.gprs[ArmGpr::kR13] = sp;
}

void ArmCpu::SetPendingResumeVector(uint32_t pc) {
    pending_resume_pc_     = pc;
    pending_resume_pc_set_ = true;
}

void ArmCpu::SetPendingResumeMmu(uint32_t control, uint32_t ttbr0, uint32_t dacr) {
    pending_resume_control_ = control;
    pending_resume_ttbr0_   = ttbr0;
    pending_resume_dacr_    = dacr;
    pending_resume_mmu_set_ = true;
}

void __cdecl ArmCpu::RaiseUndefinedExceptionHelper(ArmCpu* cpu, uint32_t guest_pc) {
    cpu->RaiseUndefinedException(guest_pc);
}

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1148: condition flags N[31] Z[30] C[29]
   V[28]. */
void __cdecl ArmCpu::UpdateNzcvOnlyHelper(ArmCpu* cpu, uint32_t nzcv_source) {
    constexpr uint32_t kNzcvMask = 0xF0000000u;
    cpu->state_.cpsr.word = (cpu->state_.cpsr.word & ~kNzcvMask) |
                            (nzcv_source & kNzcvMask);
}

void ArmCpu::SaveState(StateWriter& w) {
    w.Write<ArmCpuState>(state_);
}

void ArmCpu::RestoreState(StateReader& r) {
    r.Read(state_);
}
