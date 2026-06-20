#include "iop13xx_cp6.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/arm_jit.h"
#include "../../jit/block_context.h"
#include "../../jit/cpu_state.h"
#include "../../jit/decoded_insn.h"
#include "../../jit/place_fns.h"
#include "../../jit/x86_emit.h"
#include "../../state/emulation_freeze.h"

#include <bit>
#include <chrono>
#include <cstddef>

namespace {

constexpr uint32_t Cp6Key(uint32_t crn, uint32_t crm,
                          uint32_t opc2 = 0, uint32_t opc1 = 0) {
    return crn | (crm << 4) | (opc2 << 8) | (opc1 << 11);
}

}  /* namespace */

Iop13xxCp6::~Iop13xxCp6() {
    OnShutdown();
}

bool Iop13xxCp6::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetSoc() == SocFamily::IOP13xx;
}

void Iop13xxCp6::OnReady() {
    const uint32_t cycles = GuestCycles();
    /* The P377 boot loader leaves timer 1 as a free-running 25 MHz down
       counter; OALStallExecution reads TCR1 before OEMInit programs timer 0. */
    timer_[1].control = kTimerEnable;
    timer_[1].counter = 0xFFFFFFFFu;
    timer_[1].reload = 0xFFFFFFFFu;
    timer_[1].base_cycles = cycles;
    timer_thread_ = std::thread(&Iop13xxCp6::TimerLoop, this);
}

void Iop13xxCp6::OnShutdown() {
    stop_thread_.store(true, std::memory_order_release);
    if (timer_thread_.joinable()) timer_thread_.join();
}

uint32_t Iop13xxCp6::GuestCycles() const {
    return emu_.Get<ArmJit>().CpuState()->guest_cycle_counter;
}

bool Iop13xxCp6::HasPendingIrqLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        if ((pending_[bank] & intctl_[bank] & ~intstr_[bank]) != 0) {
            return true;
        }
    }
    return false;
}

void Iop13xxCp6::NotifyLocked() {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        if ((pending_[bank] & intctl_[bank] & intstr_[bank]) != 0) {
            LOG(SocIntc, "IOP13xx CP6: FIQ source pending in bank %u; "
                         "ArmJit currently delivers IRQ only\n", bank);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
    auto& jit = emu_.Get<ArmJit>();
    if (HasPendingIrqLocked()) jit.SetInterruptPending();
    else                       jit.ClearInterruptPending();
}

void Iop13xxCp6::AssertIrq(int source_bit) {
    if (source_bit < 0 || source_bit >= 128) {
        LOG(Caution, "Iop13xxCp6::AssertIrq: source %d outside 0..127\n",
            source_bit);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    std::lock_guard<std::mutex> guard(state_mutex_);
    const uint32_t bank = static_cast<uint32_t>(source_bit) / 32u;
    const uint32_t bit = 1u << (static_cast<uint32_t>(source_bit) & 31u);
    pending_[bank] |= bit;
    NotifyLocked();
}

void Iop13xxCp6::AssertSubIrq(int main_source_bit, int /*sub_source_bit*/) {
    AssertIrq(main_source_bit);
}

void Iop13xxCp6::DeAssertIrq(int source_bit) {
    if (source_bit < 0 || source_bit >= 128) return;
    std::lock_guard<std::mutex> guard(state_mutex_);
    const uint32_t bank = static_cast<uint32_t>(source_bit) / 32u;
    const uint32_t bit = 1u << (static_cast<uint32_t>(source_bit) & 31u);
    pending_[bank] &= ~bit;
    NotifyLocked();
}

void Iop13xxCp6::DeliverPendingIrq() {
    std::lock_guard<std::mutex> guard(state_mutex_);
    if (!HasPendingIrqLocked()) return;
    auto& jit = emu_.Get<ArmJit>();
    if (!jit.CpuState()->cpsr.bits.irq_disable) {
        jit.Cpu()->RaiseIrqException(jit.CpuState()->gprs[ArmGpr::kR15]);
    }
}

void Iop13xxCp6::AdvanceTimerLocked(Timer& timer, uint32_t cycles_now,
                                     uint32_t status_bit) {
    if ((timer.control & kTimerEnable) == 0) {
        timer.base_cycles = cycles_now;
        return;
    }
    const uint32_t elapsed_cycles = cycles_now - timer.base_cycles;
    const uint32_t elapsed_ticks = elapsed_cycles / kTimerDivider;
    if (elapsed_ticks == 0) return;

    timer.base_cycles += elapsed_ticks * kTimerDivider;
    if (elapsed_ticks < timer.counter) {
        timer.counter -= elapsed_ticks;
        return;
    }

    if (status_bit != 0) tisr_ |= status_bit;
    if ((timer.control & kTimerReload) != 0 && timer.reload != 0) {
        const uint32_t after_first = elapsed_ticks - timer.counter;
        const uint32_t remainder = after_first % timer.reload;
        timer.counter = remainder == 0 ? timer.reload
                                       : timer.reload - remainder;
    } else {
        timer.counter = 0;
        timer.control &= ~kTimerEnable;
    }
}

void Iop13xxCp6::AdvanceTimersLocked(uint32_t cycles_now) {
    AdvanceTimerLocked(timer_[0], cycles_now, 1u);
    AdvanceTimerLocked(timer_[1], cycles_now, 2u);

    const uint32_t timer_mask = 1u << kTimer0Irq;
    if ((tisr_ & 1u) != 0) pending_[0] |= timer_mask;
    else                   pending_[0] &= ~timer_mask;
}

void Iop13xxCp6::TimerLoop() {
    auto& freeze = emu_.Get<EmulationFreeze>();
    while (!stop_thread_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        auto frozen = freeze.WorkerSection();
        std::lock_guard<std::mutex> guard(state_mutex_);
        AdvanceTimersLocked(GuestCycles());
        NotifyLocked();
    }
}

uint32_t Iop13xxCp6::InterruptVectorLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        const uint32_t active = pending_[bank] & intctl_[bank] & ~intstr_[bank];
        if (active == 0) continue;
        const uint32_t source = bank * 32u + std::countr_zero(active);
        /* MP377 programs INTSIZE=1, the IOP13xx four-byte vector format. */
        const uint32_t shift = intsize_ + 1u;
        return intbase_ + (source << shift);
    }
    return 0xFFFFFFFFu;
}

uint32_t Iop13xxCp6::ReadRegisterLocked(uint32_t key,
                                         uint32_t cycles_now) {
    AdvanceTimersLocked(cycles_now);
    switch (key) {
        case Cp6Key(0, 2): return intbase_;
        case Cp6Key(2, 2): return intsize_;
        case Cp6Key(3, 2): return InterruptVectorLocked();
        case Cp6Key(0, 4): return intctl_[0];
        case Cp6Key(1, 4): return intctl_[1];
        case Cp6Key(2, 4): return intctl_[2];
        case Cp6Key(3, 4): return intctl_[3];
        case Cp6Key(0, 6): return 0u;
        case Cp6Key(1, 6): return 0u;
        case Cp6Key(0, 9): return timer_[0].control;
        case Cp6Key(1, 9): return timer_[1].control;
        case Cp6Key(2, 9): return timer_[0].counter;
        case Cp6Key(3, 9): return timer_[1].counter;
        case Cp6Key(4, 9): return timer_[0].reload;
        case Cp6Key(5, 9): return timer_[1].reload;
        case Cp6Key(6, 9): return tisr_;
        case Cp6Key(7, 9): return wdtcr_;
        case Cp6Key(8, 9): return wdtsr_;
        default:
            LOG(Caution, "IOP13xx CP6 unsupported read key 0x%04X\n", key);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void Iop13xxCp6::WriteRegisterLocked(uint32_t key, uint32_t value,
                                      uint32_t cycles_now) {
    AdvanceTimersLocked(cycles_now);
    switch (key) {
        case Cp6Key(0, 2):
            intbase_ = value;
            break;
        case Cp6Key(2, 2):
            intsize_ = value & 3u;
            break;
        case Cp6Key(0, 4):
            intctl_[0] = value;
            break;
        case Cp6Key(1, 4):
            intctl_[1] = value;
            break;
        case Cp6Key(2, 4): intctl_[2] = value; break;
        case Cp6Key(3, 4): intctl_[3] = value; break;
        case Cp6Key(0, 5):
            intstr_[0] = value;
            break;
        case Cp6Key(1, 5):
            intstr_[1] = value;
            break;
        case Cp6Key(2, 5): intstr_[2] = value; break;
        case Cp6Key(3, 5): intstr_[3] = value; break;
        case Cp6Key(0, 9): timer_[0].control = value; timer_[0].base_cycles = cycles_now; break;
        case Cp6Key(1, 9): timer_[1].control = value; timer_[1].base_cycles = cycles_now; break;
        case Cp6Key(2, 9): timer_[0].counter = value; timer_[0].base_cycles = cycles_now; break;
        case Cp6Key(3, 9): timer_[1].counter = value; timer_[1].base_cycles = cycles_now; break;
        case Cp6Key(4, 9): timer_[0].reload = value; break;
        case Cp6Key(5, 9): timer_[1].reload = value; break;
        case Cp6Key(6, 9): tisr_ &= ~value; break;
        case Cp6Key(7, 9): wdtcr_ = value; break;
        case Cp6Key(8, 9): wdtsr_ &= ~value; break;
        default:
            LOG(Caution, "IOP13xx CP6 unsupported write key 0x%04X value 0x%08X\n",
                key, value);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    AdvanceTimersLocked(cycles_now);
    NotifyLocked();
}

uint32_t __fastcall Iop13xxCp6::ReadHelper(Iop13xxCp6* self,
                                            uint32_t key) {
    std::lock_guard<std::mutex> guard(self->state_mutex_);
    return self->ReadRegisterLocked(key, self->GuestCycles());
}

void __fastcall Iop13xxCp6::WriteHelper(Iop13xxCp6* self, uint32_t key,
                                         uint32_t value) {
    std::lock_guard<std::mutex> guard(self->state_mutex_);
    self->WriteRegisterLocked(key, value, self->GuestCycles());
}

uint8_t* Iop13xxCp6::EmitRegisterTransfer(uint8_t* cursor, DecodedInsn* d,
                                           BlockContext* ctx) {
    using namespace x86;
    if (d->cp_opc != 0 || d->cp != 0 || d->rd == 15) {
        return EmitCoprocUnimplementedFatal(cursor, d, ctx);
    }

    const uint32_t key = Cp6Key(d->crn, d->crm, d->cp, d->cp_opc);
    const int32_t rd_disp = static_cast<int32_t>(
        offsetof(ArmCpuState, gprs) + d->rd * 4u);
    EmitMovRegImm32(cursor, kEcx,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)));
    EmitMovRegImm32(cursor, kEdx, key);
    if (d->l) {
        EmitCall(cursor, reinterpret_cast<void*>(&Iop13xxCp6::ReadHelper));
        EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
    } else {
        EmitPushBaseDisp32(cursor, kStateReg, rd_disp);
        EmitCall(cursor, reinterpret_cast<void*>(&Iop13xxCp6::WriteHelper));
    }
    return cursor;
}

REGISTER_SERVICE_AS(Iop13xxCp6, IrqController);
