#include "arm_routed_instruction.h"

#include "arm_mmu.h"
#include "arm_mmu_state.h"
#include "arm_routed_access.h"
#include "cpu_state.h"
#include "decoded_insn.h"

ArmRoutedInstruction::Outcome ArmRoutedInstruction::Swap(DecodedInsn* d) {
    const ArmSctlr sctlr             = mmu_->State()->effective_control_register;
    const bool     u1                = mmu_->UnalignedAccessesFault();
    const bool     is_byte           = d->n != 0u;
    const bool     align_fault_check = !is_byte && (u1 || sctlr.bits.a);
    const uint32_t bytes             = is_byte ? 1u : 4u;
    const uint32_t pc                = d->guest_address;
    const uint32_t base              = cpu_state_->gprs[d->rn];
    const uint32_t address =
        (!is_byte && !align_fault_check) ? (base & 0xFFFFFFFCu) : base;

    uint32_t value = 0;
    if (!access_->Load(cpu_state_, pc, address, bytes, &value)) {
        return Abort(d, false, 0u);
    }
    if (!is_byte && !align_fault_check) {
        const uint32_t rot = 8u * (base & 3u);
        if (rot != 0u) value = (value >> rot) | (value << (32u - rot));
    }
    if (!access_->Store(cpu_state_, pc, address, bytes,
                        cpu_state_->gprs[d->rm])) {
        return Abort(d, false, 0u);
    }
    cpu_state_->gprs[d->rd] = value;
    return Outcome::kNextInsn;
}

ArmRoutedInstruction::Outcome ArmRoutedInstruction::Exclusive(DecodedInsn* d,
                                                              bool is_store) {
    const uint32_t pc      = d->guest_address;
    const uint32_t address = cpu_state_->gprs[d->rn];

    if (!is_store) {
        uint32_t value = 0;
        if (!access_->Load(cpu_state_, pc, address, 4u, &value)) {
            return Abort(d, false, 0u);
        }
        cpu_state_->gprs[d->rd]         = value;
        cpu_state_->ldrex_monitor_addr  = address;
        cpu_state_->ldrex_monitor_armed = 1u;
        return Outcome::kNextInsn;
    }

    if (cpu_state_->ldrex_monitor_armed == 0u ||
        cpu_state_->ldrex_monitor_addr != address) {
        cpu_state_->gprs[d->rd] = 1u;
        return Outcome::kNextInsn;
    }
    if (!access_->Store(cpu_state_, pc, address, 4u,
                        cpu_state_->gprs[d->rm])) {
        return Abort(d, false, 0u);
    }
    cpu_state_->gprs[d->rd]          = 0u;
    cpu_state_->ldrex_monitor_armed  = 0u;
    return Outcome::kNextInsn;
}
