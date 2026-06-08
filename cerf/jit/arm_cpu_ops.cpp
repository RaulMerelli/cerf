#include "arm_cpu_ops.h"

#include <mutex>

#include "arm_jit.h"

namespace {

/* Swap GPRs[13]/GPRs[14] with bank[0]/bank[1], and swap SPSR.word
   with the per-mode SPSR slot. Single-mode swap helper; called by
   ArmCpuBankSwitch after dispatching on CPSR.M. */
inline void SwapBank(uint32_t* gprs,
                     uint32_t* bank_regs,
                     uint32_t* spsr_visible_word,
                     uint32_t* spsr_bank) {
    const uint32_t t13 = gprs[13];
    gprs[13]      = bank_regs[0];
    bank_regs[0]  = t13;

    const uint32_t t14 = gprs[14];
    gprs[14]      = bank_regs[1];
    bank_regs[1]  = t14;

    const uint32_t tspsr = *spsr_visible_word;
    *spsr_visible_word = *spsr_bank;
    *spsr_bank         = tspsr;
}

/* On a core without Thumb (ArmProcessorConfig::HasThumb() == false,
   e.g. SA-1110 = ARM V4 per SA-1110 Dev Manual §1.4) CPSR.T does not
   exist: MSR / SPSR-restore writes leave it 0, as on real silicon.
   jlime linexec's MSR CPSR_c, CPSR|0xEF relies on this. */
template <typename Psr>
inline void ClampThumbForCore(const ArmJit* jit, Psr* new_psr) {
    if (!jit->HasThumb()) {
        new_psr->bits.thumb_mode = 0;
    }
}

}  // namespace

void ArmCpuBankSwitch(ArmCpuState* state) {
    switch (state->cpsr.bits.mode) {
    case ArmMode::kSupervisor:
        SwapBank(state->gprs, state->gprs_svc,
                 &state->spsr.word, &state->spsr_svc);
        break;
    case ArmMode::kAbort:
        SwapBank(state->gprs, state->gprs_abt,
                 &state->spsr.word, &state->spsr_abt);
        break;
    case ArmMode::kIrq:
        SwapBank(state->gprs, state->gprs_irq,
                 &state->spsr.word, &state->spsr_irq);
        break;
    case ArmMode::kUndefined:
        SwapBank(state->gprs, state->gprs_und,
                 &state->spsr.word, &state->spsr_und);
        break;

    case ArmMode::kUser:
    case ArmMode::kSystem:
        /* No bank for these modes — GPRs[13]/[14] already hold the
           user-view values. */
        break;

    case ArmMode::kFiq:
        /* FIQ banking is not modelled; guest software in scope does
           not enable FIQ. */
        break;

    default:
        /* Mode field corrupt — leave state alone; caller will fault
           on the next instruction. */
        break;
    }
}

uint32_t ArmCpuGetCpsrWithFlags(const ArmCpuState* state) {
    const uint32_t psr_mask =
          (static_cast<uint32_t>(state->x86_overflow.byte)     << ArmPsrBit::kOverflow)
        | (state->x86_flags.bits.carry_flag                    << ArmPsrBit::kCarry)
        | (state->x86_flags.bits.zero_flag                     << ArmPsrBit::kZero)
        | (state->x86_flags.bits.sign_flag                     << ArmPsrBit::kNegative);
    return (state->cpsr.partial_word & 0x0FFFFFFFu) | psr_mask;
}

void ArmCpuUpdateFlags(ArmCpuState* state, uint32_t new_flag_value) {
    state->x86_flags.bits.carry_flag = (new_flag_value >> ArmPsrBit::kCarry)    & 0x1u;
    state->x86_flags.bits.sign_flag  = (new_flag_value >> ArmPsrBit::kNegative) & 0x1u;
    state->x86_flags.bits.zero_flag  = (new_flag_value >> ArmPsrBit::kZero)     & 0x1u;
    state->x86_overflow.word         = (new_flag_value >> ArmPsrBit::kOverflow) & 0x1u;
    state->cpsr.bits.saturate_flag   = (new_flag_value >> ArmPsrBit::kSaturate) & 0x1u;
}

void ArmCpuUpdateNzcvOnly(ArmCpuState* state, uint32_t new_flag_value) {
    state->x86_flags.bits.carry_flag = (new_flag_value >> ArmPsrBit::kCarry)    & 0x1u;
    state->x86_flags.bits.sign_flag  = (new_flag_value >> ArmPsrBit::kNegative) & 0x1u;
    state->x86_flags.bits.zero_flag  = (new_flag_value >> ArmPsrBit::kZero)     & 0x1u;
    state->x86_overflow.word         = (new_flag_value >> ArmPsrBit::kOverflow) & 0x1u;
}

void ArmCpuUpdateCpsrWithFlags(ArmJit* jit, ArmCpuState* state, ArmPsrFull new_psr) {
    ClampThumbForCore(jit, &new_psr);
    const bool mode_changed =
        state->cpsr.bits.mode != new_psr.bits.mode;

    /* First swap: parks the OLD privileged mode's bank back into
       its slot and brings user-view R13/R14 into the visible
       register file. No-op for User / System / FIQ. */
    if (mode_changed) {
        ArmCpuBankSwitch(state);
    }

    if (new_psr.bits.thumb_mode && !state->cpsr.bits.thumb_mode) {
        /* ARM → Thumb transition: PC bit 0 used to carry the
           interworking indicator; clear it now that we've consumed
           it into CPSR.T. */
        state->gprs[ArmGpr::kR15] &= 0xFFFFFFFEu;
    }

    if (state->cpsr.bits.irq_disable != new_psr.bits.irq_disable) {
        std::lock_guard<std::mutex> guard(jit->InterruptLock());
        state->cpsr.partial_word = new_psr.word & 0x0FFFFFFFu;
        jit->UpdateInterruptOnPoll();
    } else {
        state->cpsr.partial_word = new_psr.word & 0x0FFFFFFFu;
    }

    state->x86_flags.bits.carry_flag = new_psr.bits.carry_flag;
    state->x86_flags.bits.sign_flag  = new_psr.bits.negative_flag;
    state->x86_flags.bits.zero_flag  = new_psr.bits.zero_flag;
    state->x86_overflow.word         = new_psr.bits.overflow_flag;

    /* Second swap: with CPSR.M now set to the NEW mode, ArmCpuBankSwitch
       brings that mode's stored R13/R14 into the visible register
       file and parks the user-view values into the bank slot. */
    if (mode_changed) {
        ArmCpuBankSwitch(state);
    }
}

void ArmCpuUpdateCpsr(ArmJit* jit, ArmCpuState* state, ArmPsr new_psr) {
    ClampThumbForCore(jit, &new_psr);
    const bool mode_changed =
        state->cpsr.bits.mode != new_psr.bits.mode;

    /* First swap: parks the OLD privileged mode's bank back into
       its slot and brings user-view R13/R14 into the visible
       register file. No-op for User / System / FIQ. */
    if (mode_changed) {
        ArmCpuBankSwitch(state);
    }

    if (new_psr.bits.thumb_mode && !state->cpsr.bits.thumb_mode) {
        state->gprs[ArmGpr::kR15] &= 0xFFFFFFFEu;
    }

    /* IRQ-disable change — same lock-and-patch pattern as the
       WithFlags variant. */
    if (state->cpsr.bits.irq_disable != new_psr.bits.irq_disable) {
        std::lock_guard<std::mutex> guard(jit->InterruptLock());
        state->cpsr.partial_word = new_psr.partial_word;
        jit->UpdateInterruptOnPoll();
    } else {
        state->cpsr.partial_word = new_psr.partial_word;
    }

    /* Second swap: with CPSR.M now set to the NEW mode, ArmCpuBankSwitch
       brings that mode's stored R13/R14 into the visible register
       file and parks the user-view values into the bank slot. */
    if (mode_changed) {
        ArmCpuBankSwitch(state);
    }
}
