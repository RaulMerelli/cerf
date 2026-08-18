#include "arm_exception_frame.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "arm_cpu.h"
#include "arm_mmu.h"
#include "arm_page_walker.h"
#include "cpu_state.h"

REGISTER_SERVICE(ArmExceptionFrame);

void ArmExceptionFrame::OnReady() {
    cpu_       = &emu_.Get<ArmCpu>();
    cpu_state_ = cpu_->State();
    mmu_       = &emu_.Get<ArmMmu>();
    walker_    = &emu_.Get<ArmPageWalker>();
}

uint32_t __fastcall ArmExceptionFrame::RfeHelper(uint32_t           rn_value,
                                                 uint32_t           encoded,
                                                 ArmExceptionFrame* frame) {
    const bool     p_bit = (encoded & 0x80u) != 0u;
    const bool     u_bit = (encoded & 0x40u) != 0u;
    const bool     w_bit = (encoded & 0x20u) != 0u;
    const uint32_t rn    = encoded & 0x1Fu;

    /* ddi0406c B9.3.13 RFE pseudocode:
         address = if increment then R[n] else R[n]-8;
         if wordhigher then address = address+4; */
    uint32_t address = u_bit ? rn_value : (rn_value - 8u);
    if (p_bit == u_bit) {
        address += 4u;
    }

    ArmCpuState* state = frame->cpu_state_;

    /* ddi0406c B9.3.13 Operation: "if CurrentModeIsHyp() then UNDEFINED;
       elsif (!CurrentModeIsNotUser() ...) then UNPREDICTABLE". System mode
       is explicitly permitted. */
    const uint32_t current_mode = state->cpsr.bits.mode;
    if (current_mode == ArmMode::kUser || current_mode == ArmMode::kHyp) {
        LOG(Caution, "RfeHelper: executed in CPSR.M=0x%02X - UNDEFINED/"
                     "UNPREDICTABLE per ddi0406c B9.3.13\n", current_mode);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    /* ddi0406c B3.15.5 (p. B3-1461) + Glossary "Context synchronization
       operation": returning from an exception synchronizes as its first
       step. */
    frame->mmu_->SynchronizeSctlr();

    if (frame->mmu_->AlignMultiWordOrFault(address, /*is_write=*/false)) {
        return 1u;
    }

    uint8_t* host_pc_ptr = frame->walker_->TranslateRead(state, address);
    if (!host_pc_ptr) {
        LOG(Caution, "RfeHelper: TranslateRead failed for VA 0x%08X (new_pc slot) "
                      "- RFE from non-RAM or aborted load not supported\n", address);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t new_pc = *reinterpret_cast<uint32_t*>(host_pc_ptr);

    uint8_t* host_cpsr_ptr = frame->walker_->TranslateRead(state, address + 4u);
    if (!host_cpsr_ptr) {
        LOG(Caution, "RfeHelper: TranslateRead failed for VA 0x%08X (new_cpsr slot)"
                      " - RFE from non-RAM or aborted load not supported\n",
                      address + 4u);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t new_cpsr = *reinterpret_cast<uint32_t*>(host_cpsr_ptr);

    /* Writeback BEFORE the CPSR change so it lands in the OLD mode's
       bank (ddi0406c B9.3.13 pseudocode ordering). */
    if (w_bit) {
        state->gprs[rn] = u_bit ? (rn_value + 8u) : (rn_value - 8u);
    }

    /* ddi0406c B9.3.13 Operation (p. B9-2001):
         CPSRWriteByInstr(spsr_value, '1111', TRUE);
         ... else BranchWritePC(new_pc_value); */
    state->gprs[ArmGpr::kR15] =
        frame->cpu_->ReturnFromException(new_cpsr, new_pc);
    return 0u;
}

uint32_t __fastcall ArmExceptionFrame::SrsHelper(uint32_t           encoded,
                                                 ArmExceptionFrame* frame,
                                                 uint32_t           guest_pc) {
    const bool     p_bit       = (encoded & 0x80u) != 0u;
    const bool     u_bit       = (encoded & 0x40u) != 0u;
    const bool     w_bit       = (encoded & 0x20u) != 0u;
    const uint32_t target_mode = encoded & 0x1Fu;

    ArmCpuState*   state        = frame->cpu_state_;
    const uint32_t current_mode = state->cpsr.bits.mode;

    if (current_mode == ArmMode::kUser || current_mode == ArmMode::kSystem) {
        LOG(Caution, "SrsHelper pc=0x%08X: executed in User/System mode "
                      "(CPSR.M=0x%X) - UNPREDICTABLE\n",
                      guest_pc, current_mode);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    /* ddi0406c B9.3.16: target Hyp (0x1A) is UNPREDICTABLE. */
    if (target_mode == 0x1Au) {
        LOG(Caution, "SrsHelper pc=0x%08X: target Hyp mode (0x1A) - UNPREDICTABLE\n",
            guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    uint32_t* base_ptr = frame->cpu_->BankedSp(target_mode);

    const uint32_t base    = *base_ptr;
    uint32_t       address = u_bit ? base : (base - 8u);
    if (p_bit == u_bit) {
        address += 4u;
    }

    if (frame->mmu_->AlignMultiWordOrFault(address, /*is_write=*/true)) {
        return 1u;
    }

    uint8_t* host_lr = frame->walker_->TranslateWrite(state, address);
    if (!host_lr) {
        LOG(Caution, "SrsHelper pc=0x%08X: TranslateWrite failed for LR slot "
                      "VA 0x%08X - SRS to non-RAM or aborted store not supported\n",
                      guest_pc, address);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    *reinterpret_cast<uint32_t*>(host_lr) = state->gprs[ArmGpr::kR14];

    uint8_t* host_spsr = frame->walker_->TranslateWrite(state, address + 4u);
    if (!host_spsr) {
        LOG(Caution, "SrsHelper pc=0x%08X: TranslateWrite failed for SPSR slot "
                      "VA 0x%08X - SRS to non-RAM or aborted store not supported\n",
                      guest_pc, address + 4u);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    *reinterpret_cast<uint32_t*>(host_spsr) =
        frame->cpu_->BankedSpsr(state->cpsr.bits.mode)->word;

    if (w_bit) {
        *base_ptr = u_bit ? (base + 8u) : (base - 8u);
    }
    return 0u;
}
