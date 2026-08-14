#include "arm_routed_instruction.h"

#include <cstring>
#include <intrin.h>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_cpu.h"
#include "arm_decoder.h"
#include "arm_mmu.h"
#include "arm_mmu_state.h"
#include "arm_opcode.h"
#include "arm_page_walker.h"
#include "arm_routed_access.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb_decoder.h"

REGISTER_SERVICE(ArmRoutedInstruction);

bool ArmRoutedInstruction::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmRoutedInstruction::OnReady() {
    cpu_       = &emu_.Get<ArmCpu>();
    cpu_state_ = cpu_->State();
    decoder_   = &emu_.Get<ArmDecoder>();
    mmu_       = &emu_.Get<ArmMmu>();
    walker_    = &emu_.Get<ArmPageWalker>();
    config_    = &emu_.Get<ArmProcessorConfig>();
    access_    = &emu_.Get<ArmRoutedAccess>();
    thumb_     = &emu_.Get<ThumbDecoder>();
}

void ArmRoutedInstruction::Complete(uint32_t guest_pc) {
    const bool thumb = cpu_state_->cpsr.bits.thumb_mode != 0u;
    uint8_t*   host  = walker_->TranslateExecute(cpu_state_, guest_pc);
    if (host == nullptr) {
        LOG(Caution, "ArmRoutedInstruction: guest PC 0x%08X is unmapped on "
                "re-fetch\n", guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    DecodedInsn d;
    std::memset(&d, 0, sizeof(d));
    d.guest_address        = guest_pc;
    d.actual_guest_address = guest_pc;

    uint32_t raw     = 0;
    bool     decoded = false;
    if (thumb) {
        uint16_t half = 0;
        std::memcpy(&half, host, sizeof(half));
        raw     = half;
        decoded = thumb_->DecodeThumb(&d, half);
    } else {
        ArmOpcode op;
        std::memcpy(&op.word, host, sizeof(op.word));
        raw     = op.word;
        decoded = decoder_->DecodeArm(&d, op);
    }
    if (!decoded) {
        LOG(Caution, "ArmRoutedInstruction: guest PC 0x%08X word 0x%08X no "
                "longer decodes\n", guest_pc, raw);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    Outcome outcome;
    if (d.place_fn == &PlaceSingleDataTransfer) {
        outcome = SingleTransfer(&d);
    } else if (d.place_fn == &PlaceLoadStoreExtension) {
        outcome = d.op1 == 0u ? Swap(&d) : HalfwordTransfer(&d);
    } else if (d.place_fn == &PlaceBlockDataTransfer) {
        outcome = BlockTransfer(&d);
    } else if (d.place_fn == &PlaceLdrex) {
        outcome = Exclusive(&d, false);
    } else if (d.place_fn == &PlaceStrex) {
        outcome = Exclusive(&d, true);
    } else {
        LOG(Caution, "ArmRoutedInstruction: guest PC 0x%08X word 0x%08X routes "
                "a peripheral access from an instruction family with no "
                "completion\n", guest_pc, raw);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    if (outcome == Outcome::kNextInsn) {
        cpu_state_->gprs[ArmGpr::kR15] = guest_pc + (thumb ? 2u : 4u);
    }
}

/* ARM DDI 0100I A7.1, p. A7-3. */
uint32_t ArmRoutedInstruction::PcReadValue(const DecodedInsn* d) const {
    return d->guest_address +
           (cpu_state_->cpsr.bits.thumb_mode != 0u ? 4u : 8u);
}

ArmRoutedInstruction::Outcome ArmRoutedInstruction::Abort(
    DecodedInsn* d, bool wback, uint32_t base_on_abort) {
    if (wback && !config_->BaseRestoredAbortModel()) {
        cpu_state_->gprs[d->rn] = base_on_abort;
    }
    cpu_->RaiseAbortDataException(d->guest_address);
    return Outcome::kAborted;
}

void ArmRoutedInstruction::LoadWritePc(uint32_t value) {
    if (config_->HasLoadToPcInterworking()) {
        if ((value & 1u) != 0u) {
            cpu_state_->cpsr.bits.thumb_mode = 1u;
            value &= 0xFFFFFFFEu;
        } else {
            cpu_state_->cpsr.bits.thumb_mode = 0u;
            value &= 0xFFFFFFFCu;
        }
    } else {
        value &= cpu_state_->cpsr.bits.thumb_mode ? 0xFFFFFFFEu : 0xFFFFFFFCu;
    }
    cpu_state_->gprs[ArmGpr::kR15] = value;
}

uint32_t ArmRoutedInstruction::SingleShiftedOffset(const DecodedInsn* d) {
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

uint32_t ArmRoutedInstruction::SingleOffsetAddr(const DecodedInsn* d) {
    if (d->n != 0u) {
        if (d->rn == ArmGpr::kR15) {
            return PcReadValue(d) + static_cast<uint32_t>(d->offset);
        }
        return cpu_state_->gprs[d->rn] + static_cast<uint32_t>(d->offset);
    }
    const uint32_t offset = SingleShiftedOffset(d);
    const uint32_t base   = (d->rn == ArmGpr::kR15)
        ? PcReadValue(d)
        : cpu_state_->gprs[d->rn];
    return d->u != 0u ? base + offset : base - offset;
}

uint32_t ArmRoutedInstruction::HalfwordOffsetAddr(const DecodedInsn* d) {
    const uint32_t base = (d->rn == ArmGpr::kR15)
        ? PcReadValue(d)
        : cpu_state_->gprs[d->rn];
    if (d->n != 0u) {
        return base + static_cast<uint32_t>(d->offset);
    }
    const uint32_t offset = cpu_state_->gprs[d->rm];
    return d->u != 0u ? base + offset : base - offset;
}

ArmRoutedInstruction::Outcome ArmRoutedInstruction::SingleTransfer(
    DecodedInsn* d) {
    const bool     wback       = d->p == 0u || d->w != 0u;
    const uint32_t bytes       = d->s != 0u ? 1u : 4u;
    const uint32_t offset_addr = SingleOffsetAddr(d);
    uint32_t address           =
        d->p != 0u ? offset_addr : cpu_state_->gprs[d->rn];

    const ArmSctlr sctlr = mmu_->State()->effective_control_register;
    uint32_t       rot   = 0;
    if (bytes == 4u) {
        if (sctlr.bits.a) {
            if ((address & 3u) != 0u) {
                mmu_->RaiseAlignmentFault(address, d->l == 0u);
                return Abort(d, wback, offset_addr);
            }
        } else if (!mmu_->UnalignedAccessesFault()) {
            rot      = (address & 3u) * 8u;
            address &= 0xFFFFFFFCu;
        }
    }

    if (d->l != 0u) {
        uint32_t value = 0;
        if (!access_->Load(cpu_state_, d->guest_address, address, bytes,
                           &value)) {
            return Abort(d, wback, offset_addr);
        }
        if (rot != 0u) {
            value = (value >> rot) | (value << (32u - rot));
        }
        if (wback) {
            cpu_state_->gprs[d->rn] = offset_addr;
        }
        if (d->rd == ArmGpr::kR15) {
            LoadWritePc(value);
            return Outcome::kPcWritten;
        }
        cpu_state_->gprs[d->rd] = value;
        return Outcome::kNextInsn;
    }

    const uint32_t value = (d->rd == ArmGpr::kR15)
        ? d->guest_address + config_->PcStoreOffset()
        : cpu_state_->gprs[d->rd];
    if (!access_->Store(cpu_state_, d->guest_address, address, bytes, value)) {
        return Abort(d, wback, offset_addr);
    }
    if (wback) {
        cpu_state_->gprs[d->rn] = offset_addr;
    }
    return Outcome::kNextInsn;
}

ArmRoutedInstruction::Outcome ArmRoutedInstruction::HalfwordTransfer(
    DecodedInsn* d) {
    const bool     wback       = d->p == 0u || d->w != 0u;
    const uint32_t offset_addr = HalfwordOffsetAddr(d);
    const uint32_t address     =
        d->p != 0u ? offset_addr : cpu_state_->gprs[d->rn];
    const uint32_t pc          = d->guest_address;
    uint32_t       value       = 0;

    const ArmSctlr sctlr       = mmu_->State()->effective_control_register;
    const bool     load        = d->l != 0u;
    const bool     is_store    = !load && (d->op1 == 1u || d->op1 == 3u);
    const bool     is_dual     = !load && (d->op1 == 2u || d->op1 == 3u);
    const bool     is_halfword =
        d->op1 == 1u || (load && d->op1 == 3u);
    if (is_dual) {
        const uint32_t mask = mmu_->DoublewordAlignMask();
        if ((address & mask) != 0u) {
            mmu_->RaiseAlignmentFault(address, is_store);
            return Abort(d, wback, offset_addr);
        }
    } else if (is_halfword && sctlr.bits.a && (address & 1u) != 0u) {
        mmu_->RaiseAlignmentFault(address, is_store);
        return Abort(d, wback, offset_addr);
    }

    switch ((d->op1 << 1) | (d->l != 0u ? 1u : 0u)) {
    case 3u:
        if (!access_->Load(cpu_state_, pc, address, 2u, &value)) {
            return Abort(d, wback, offset_addr);
        }
        break;
    case 5u:
        if (!access_->Load(cpu_state_, pc, address, 1u, &value)) {
            return Abort(d, wback, offset_addr);
        }
        value = static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<int8_t>(value)));
        break;
    case 7u:
        if (!access_->Load(cpu_state_, pc, address, 2u, &value)) {
            return Abort(d, wback, offset_addr);
        }
        value = static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<int16_t>(value)));
        break;
    case 2u:
        if (!access_->Store(cpu_state_, pc, address, 2u,
                            cpu_state_->gprs[d->rd])) {
            return Abort(d, wback, offset_addr);
        }
        if (wback) {
            cpu_state_->gprs[d->rn] = offset_addr;
        }
        return Outcome::kNextInsn;
    case 4u: {
        /* B1.9.9 (p. B1-1217) restores the base on a Data Abort when the
           loaded list includes it, and A8.8.72 A1 (p. A8-426) makes
           Rt == Rn UNPREDICTABLE only when wback. */
        uint32_t first = 0;
        if (!access_->Load(cpu_state_, pc, address, 4u, &first)) {
            return Abort(d, wback, offset_addr);
        }
        if (!access_->Load(cpu_state_, pc, address + 4u, 4u, &value)) {
            return Abort(d, wback, offset_addr);
        }
        cpu_state_->gprs[d->rd]      = first;
        cpu_state_->gprs[d->rd + 1u] = value;
        if (wback) {
            cpu_state_->gprs[d->rn] = offset_addr;
        }
        return Outcome::kNextInsn;
    }
    case 6u:
        if (!access_->Store(cpu_state_, pc, address, 4u,
                            cpu_state_->gprs[d->rd])) {
            return Abort(d, wback, offset_addr);
        }
        if (!access_->Store(cpu_state_, pc, address + 4u, 4u,
                            cpu_state_->gprs[d->rd + 1u])) {
            return Abort(d, wback, offset_addr);
        }
        if (wback) {
            cpu_state_->gprs[d->rn] = offset_addr;
        }
        return Outcome::kNextInsn;
    default:
        LOG(Caution, "ArmRoutedInstruction: guest PC 0x%08X halfword op1=%u "
                "L=%u outside the A5.2.8 op2 space\n", pc, d->op1, d->l);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    if (wback) {
        cpu_state_->gprs[d->rn] = offset_addr;
    }
    cpu_state_->gprs[d->rd] = value;
    return Outcome::kNextInsn;
}

ArmRoutedInstruction::Outcome ArmRoutedInstruction::BlockTransfer(
    DecodedInsn* d) {
    const uint32_t list       = d->register_list;
    const uint32_t count      = __popcnt16(d->register_list);
    const bool     load       = d->l != 0u;
    const bool     pc_in_list = (list & 0x8000u) != 0u;
    const bool     exc_return = d->s != 0u && load && pc_in_list;
    const bool     user_regs  = d->s != 0u && !exc_return;
    const bool     rn_in_list = ((list >> d->rn) & 1u) != 0u;
    const bool     defer_rn   = load && rn_in_list;
    const bool     do_wback   = d->w != 0u && !(load && rn_in_list);
    const uint32_t pc         = d->guest_address;
    const uint32_t delta      = d->u != 0u
        ? 4u * count
        : static_cast<uint32_t>(-static_cast<int32_t>(4u * count));

    int32_t base_off = d->u != 0u
        ? 0
        : -static_cast<int32_t>(4u * count);
    if (d->p == d->u) {
        base_off += 4;
    }
    uint32_t address = cpu_state_->gprs[d->rn] + static_cast<uint32_t>(base_off);

    const ArmSctlr sctlr        = mmu_->State()->effective_control_register;
    const bool     always_fault = mmu_->UnalignedAccessesFault();
    if (!always_fault && !sctlr.bits.a) {
        address &= 0xFFFFFFFCu;
    } else if ((address & 3u) != 0u) {
        mmu_->RaiseAlignmentFault(address, !load);
        return Abort(d, do_wback, cpu_state_->gprs[d->rn] + delta);
    }

    uint32_t rn_word_addr = 0;
    bool     rn_word_seen = false;
    for (uint32_t i = 0; i < 16u; ++i) {
        if (((list >> i) & 1u) == 0u) {
            continue;
        }
        if (defer_rn && i == d->rn) {
            rn_word_addr = address;
            rn_word_seen = true;
            address += 4u;
            continue;
        }
        if (load) {
            uint32_t value = 0;
            if (!access_->Load(cpu_state_, pc, address, 4u, &value)) {
                return Abort(d, do_wback, cpu_state_->gprs[d->rn] + delta);
            }
            if (i == ArmGpr::kR15) {
                cpu_state_->gprs[ArmGpr::kR15] = value;
            } else if (user_regs) {
                ArmCpu::WriteUserRegHelper(cpu_, i, value);
            } else {
                cpu_state_->gprs[i] = value;
            }
        } else {
            uint32_t value;
            if (i == ArmGpr::kR15) {
                value = pc + config_->PcStoreOffset();
            } else if (user_regs) {
                value = ArmCpu::ReadUserRegHelper(cpu_, i);
            } else {
                value = cpu_state_->gprs[i];
            }
            if (!access_->Store(cpu_state_, pc, address, 4u, value)) {
                return Abort(d, do_wback, cpu_state_->gprs[d->rn] + delta);
            }
        }
        address += 4u;
    }

    if (rn_word_seen) {
        uint32_t value = 0;
        if (!access_->Load(cpu_state_, pc, rn_word_addr, 4u, &value)) {
            return Abort(d, do_wback, cpu_state_->gprs[d->rn] + delta);
        }
        if (user_regs) {
            ArmCpu::WriteUserRegHelper(cpu_, d->rn, value);
        } else {
            cpu_state_->gprs[d->rn] = value;
        }
    }

    if (do_wback) {
        cpu_state_->gprs[d->rn] += delta;
    }

    if (load && pc_in_list) {
        const uint32_t loaded = cpu_state_->gprs[ArmGpr::kR15];
        if (exc_return) {
            cpu_state_->gprs[ArmGpr::kR15] =
                ArmCpu::ExceptionReturnHelper(cpu_, loaded);
        } else {
            LoadWritePc(loaded);
        }
        return Outcome::kPcWritten;
    }
    return Outcome::kNextInsn;
}

ArmRoutedInstruction::Outcome ArmRoutedInstruction::Swap(DecodedInsn* d) {
    const ArmSctlr sctlr             = mmu_->State()->effective_control_register;
    const bool     u1                = mmu_->UnalignedAccessesFault();
    const bool     is_byte           = d->n != 0u;
    const bool     align_fault_check = !is_byte && (u1 || sctlr.bits.a);
    const uint32_t bytes             = is_byte ? 1u : 4u;
    const uint32_t pc                = d->guest_address;
    const uint32_t base              = cpu_state_->gprs[d->rn];
    const uint32_t address           =
        (!is_byte && !align_fault_check) ? (base & 0xFFFFFFFCu) : base;

    uint32_t value = 0;
    if (!access_->Load(cpu_state_, pc, address, bytes, &value)) {
        return Abort(d, false, 0u);
    }
    if (!is_byte && !align_fault_check) {
        const uint32_t rot = 8u * (base & 3u);
        if (rot != 0u) {
            value = (value >> rot) | (value << (32u - rot));
        }
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
        cpu_state_->gprs[d->rd]        = value;
        cpu_state_->ldrex_monitor_addr = address;
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
    cpu_state_->gprs[d->rd]         = 0u;
    cpu_state_->ldrex_monitor_armed = 0u;
    return Outcome::kNextInsn;
}
