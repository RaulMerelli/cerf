#include "arm_routed_addressing.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "arm_cpu.h"
#include "cpu_state.h"
#include "decoded_insn.h"

REGISTER_SERVICE(ArmRoutedAddressing);

bool ArmRoutedAddressing::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmRoutedAddressing::OnReady() {
    cpu_state_ = emu_.Get<ArmCpu>().State();
}

/* ARM DDI 0100I A7.1, p. A7-3. */
uint32_t ArmRoutedAddressing::PcReadValue(const DecodedInsn* d) const {
    return d->guest_address +
           (cpu_state_->cpsr.bits.thumb_mode != 0u ? 4u : 8u);
}

uint32_t ArmRoutedAddressing::SingleShiftedOffset(const DecodedInsn* d) const {
    const uint32_t v = cpu_state_->gprs[d->rm];
    switch (d->op1) {
    case 0u:
        return d->rs != 0u ? (v << d->rs) : v;
    case 1u:
        return d->rs != 0u ? (v >> d->rs) : 0u;
    case 2u:
        return static_cast<uint32_t>(
            static_cast<int32_t>(v) >> (d->rs != 0u ? d->rs : 31u));
    default:
        if (d->rs != 0u) {
            return (v >> d->rs) | (v << (32u - d->rs));
        }
        return (v >> 1) | (static_cast<uint32_t>(cpu_state_->cf) << 31);
    }
}

uint32_t ArmRoutedAddressing::SingleOffsetAddr(const DecodedInsn* d) const {
    if (d->n != 0u) {
        if (d->rn == ArmGpr::kR15) {
            /* A8.8.64 LDR (literal) Operation (p. A8-411): "base =
               Align(PC,4)". */
            return (PcReadValue(d) & ~3u) + static_cast<uint32_t>(d->offset);
        }
        return cpu_state_->gprs[d->rn] + static_cast<uint32_t>(d->offset);
    }
    const uint32_t offset = SingleShiftedOffset(d);
    const uint32_t base   = (d->rn == ArmGpr::kR15)
        ? PcReadValue(d)
        : cpu_state_->gprs[d->rn];
    return d->u != 0u ? base + offset : base - offset;
}

uint32_t ArmRoutedAddressing::HalfwordOffsetAddr(const DecodedInsn* d) const {
    const uint32_t base = (d->rn == ArmGpr::kR15)
        ? PcReadValue(d)
        : cpu_state_->gprs[d->rn];
    if (d->n != 0u) {
        return base + static_cast<uint32_t>(d->offset);
    }
    const uint32_t offset = cpu_state_->gprs[d->rm];
    return d->u != 0u ? base + offset : base - offset;
}
