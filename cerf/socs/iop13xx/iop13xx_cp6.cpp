#include "iop13xx_cp6.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../guest_cpu_reset.h"
#include "../../jit/arm/arm_cpu.h"
#include "../../jit/arm/arm_jit.h"
#include "../../jit/arm/block_context.h"
#include "../../jit/arm/cpu_state.h"
#include "../../jit/arm/decoded_insn.h"
#include "../../jit/arm/place_fns.h"
#include "../../jit/x86_emit.h"
#include "../../state/emulation_freeze.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <iterator>

namespace {

constexpr uint32_t Cp6Key(uint32_t crn, uint32_t crm, uint32_t opc2 = 0, uint32_t opc1 = 0) {
    return crn | (crm << 4) | (opc2 << 8) | (opc1 << 11);
}

/* siemens_mp377_v1040 nk.exe sub_804451B0/sub_80445228,
   sub_804452E8/sub_80445324, sub_8044536C/sub_804453A0;
   Linux arch/arm/mach-iop13xx irq.c, include/mach/time.h,
   include/mach/iop13xx.h and msi.c. */
constexpr uint32_t kCp6IntBase = Cp6Key(0, 2);
constexpr uint32_t kCp6IntSize = Cp6Key(2, 2);
constexpr uint32_t kCp6IntVec = Cp6Key(3, 2);

constexpr uint32_t kCp6IntCtl0 = Cp6Key(0, 4);
constexpr uint32_t kCp6IntCtl1 = Cp6Key(1, 4);
constexpr uint32_t kCp6IntCtl2 = Cp6Key(2, 4);
constexpr uint32_t kCp6IntCtl3 = Cp6Key(3, 4);

constexpr uint32_t kCp6IntStr0 = Cp6Key(0, 5);
constexpr uint32_t kCp6IntStr1 = Cp6Key(1, 5);
constexpr uint32_t kCp6IntStr2 = Cp6Key(2, 5);
constexpr uint32_t kCp6IntStr3 = Cp6Key(3, 5);

constexpr uint32_t kCp6Timer0Control = Cp6Key(0, 9);
constexpr uint32_t kCp6Timer1Control = Cp6Key(1, 9);
constexpr uint32_t kCp6Timer0Counter = Cp6Key(2, 9);
constexpr uint32_t kCp6Timer1Counter = Cp6Key(3, 9);
constexpr uint32_t kCp6Timer0Reload = Cp6Key(4, 9);
constexpr uint32_t kCp6Timer1Reload = Cp6Key(5, 9);
constexpr uint32_t kCp6TimerStatus = Cp6Key(6, 9);
constexpr uint32_t kCp6WatchdogCtrl = Cp6Key(7, 9);
constexpr uint32_t kCp6WatchdogStat = Cp6Key(8, 9);

uint64_t SteadyMicros() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

constexpr uint32_t kMp377PowerFailIrqSource = 0x1Fu;
constexpr uint32_t kMp377PowerFailIrqMask = 1u << kMp377PowerFailIrqSource;

} /* namespace */

Iop13xxCp6::~Iop13xxCp6() {
    OnShutdown();
}

bool Iop13xxCp6::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::IOP13xx;
}

void Iop13xxCp6::OnReady() {
    timer_epoch_us_ = SteadyMicros();
    {
        std::lock_guard<std::mutex> guard(state_mutex_);
        ResetStateLocked(TimerTicks(), true);
    }
    LOG(SocIntc, "IOP13xx CP6 timers: host-monotonic 25 MHz timebase\n");
    timer_thread_ = std::thread(&Iop13xxCp6::TimerLoop, this);

    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        timer_epoch_us_ = SteadyMicros();
        {
            std::lock_guard<std::mutex> guard(state_mutex_);
            ResetStateLocked(TimerTicks(), true);
        }
        NotifyLocked();
    });
}

void Iop13xxCp6::OnShutdown() {
    stop_thread_.store(true, std::memory_order_release);
    if (timer_thread_.joinable()) timer_thread_.join();
}

uint32_t Iop13xxCp6::TimerTicks() const {
    if (timer_epoch_us_ == 0) return 0;
    const uint64_t elapsed_us = SteadyMicros() - timer_epoch_us_;
    return static_cast<uint32_t>(elapsed_us * kTimerTicksPerMicrosecond);
}

bool Iop13xxCp6::HasPendingIrqLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        if ((pending_[bank] & intctl_[bank] & ~intstr_[bank]) != 0) {
            return true;
        }
    }
    return false;
}

uint32_t Iop13xxCp6::ActiveIrqSourcesBank0() const {
    std::lock_guard<std::mutex> guard(state_mutex_);
    return pending_[0] & intctl_[0] & ~intstr_[0];
}

bool Iop13xxCp6::HasPendingFiqLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        if ((pending_[bank] & intctl_[bank] & intstr_[bank]) != 0) {
            return true;
        }
    }
    return false;
}

void Iop13xxCp6::ResetStateLocked(uint32_t ticks_now, bool seed_boot_timer1) {
    std::fill(std::begin(intctl_), std::end(intctl_), 0u);
    std::fill(std::begin(intstr_), std::end(intstr_), 0u);
    std::fill(std::begin(pending_), std::end(pending_), 0u);
    intbase_ = 0;
    intsize_ = 0;
    tisr_ = 0;
    wdtcr_ = 0;
    wdtsr_ = 0;
    timer_[0] = Timer{};
    timer_[1] = Timer{};

    if (seed_boot_timer1) {
        timer_[1].control = kTimerEnable;
        timer_[1].counter = 0xFFFFFFFFu;
        timer_[1].reload = 0xFFFFFFFFu;
        timer_[1].base_cycles = ticks_now;
    }
}

void Iop13xxCp6::NotifyLocked() {
    if (HasPendingFiqLocked()) {
        emu_.Get<Fatal>().Die("IOP13xx CP6: FIQ source pending; "
                              "MP377/P377 OAL does not route sources to FIQ and "
                              "ArmJit currently delivers IRQ only");
    }
    auto& jit = emu_.Get<ArmJit>();
    if (HasPendingIrqLocked())
        jit.SetInterruptPending();
    else
        jit.ClearInterruptPending();
}

void Iop13xxCp6::AssertIrq(int source_bit) {
    if (source_bit < 0 || source_bit >= 128) {
        emu_.Get<Fatal>().Die("Iop13xxCp6::AssertIrq: source %d outside 0..127", source_bit);
    }
    std::lock_guard<std::mutex> guard(state_mutex_);
    const uint32_t bank = static_cast<uint32_t>(source_bit) / 32u;
    const uint32_t bit = 1u << (static_cast<uint32_t>(source_bit) & 31u);
    pending_[bank] |= bit;
    NotifyLocked();
}

void Iop13xxCp6::AssertSubIrq(int main_source_bit, int sub_source_bit) {
    emu_.Get<Fatal>().Die("Iop13xxCp6::AssertSubIrq: unsupported sub-source main=%d sub=%d", main_source_bit,
                          sub_source_bit);
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
    auto& cpu = emu_.Get<ArmCpu>();
    ArmCpuState* state = cpu.State();
    if (!state->cpsr.bits.irq_disable) {
        cpu.RaiseIrqException(state->gprs[ArmGpr::kR15]);
    }
}

void Iop13xxCp6::AdvanceTimerLocked(Timer& timer, uint32_t ticks_now, uint32_t status_bit) {
    if ((timer.control & kTimerEnable) == 0) {
        timer.base_cycles = ticks_now;
        return;
    }
    const uint32_t elapsed_ticks = ticks_now - timer.base_cycles;
    if (elapsed_ticks == 0) return;

    timer.base_cycles += elapsed_ticks;
    if (elapsed_ticks < timer.counter) {
        timer.counter -= elapsed_ticks;
        return;
    }

    if (status_bit != 0) tisr_ |= status_bit;
    if ((timer.control & kTimerReload) != 0 && timer.reload != 0) {
        const uint32_t after_first = elapsed_ticks - timer.counter;
        const uint32_t remainder = after_first % timer.reload;
        timer.counter = remainder == 0 ? timer.reload : timer.reload - remainder;
    } else {
        timer.counter = 0;
        timer.control &= ~kTimerEnable;
    }
}

void Iop13xxCp6::AdvanceTimersLocked(uint32_t ticks_now) {
    AdvanceTimerLocked(timer_[0], ticks_now, 1u);
    AdvanceTimerLocked(timer_[1], ticks_now, 2u);

    const uint32_t timer_mask = 1u << kTimer0Irq;
    if ((tisr_ & 1u) != 0)
        pending_[0] |= timer_mask;
    else
        pending_[0] &= ~timer_mask;
}

void Iop13xxCp6::TimerLoop() {
    auto& freeze = emu_.Get<EmulationFreeze>();
    while (!stop_thread_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        auto frozen = freeze.WorkerSection();
        std::lock_guard<std::mutex> guard(state_mutex_);
        AdvanceTimersLocked(TimerTicks());
        NotifyLocked();
    }
}

uint32_t Iop13xxCp6::InterruptVectorLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        const uint32_t active = pending_[bank] & intctl_[bank] & ~intstr_[bank];
        if (active == 0) continue;
        const uint32_t source = bank * 32u + std::countr_zero(active);
        const uint32_t shift = intsize_ + 1u;
        const uint32_t vector = intbase_ + (source << shift);
        return vector;
    }
    return 0xFFFFFFFFu;
}

uint32_t Iop13xxCp6::ReadRegisterLocked(uint32_t key, uint32_t cycles_now) {
    AdvanceTimersLocked(cycles_now);
    switch (key) {
    case kCp6IntBase: return intbase_;
    case kCp6IntSize: return intsize_;
    case kCp6IntVec: return InterruptVectorLocked();
    case kCp6IntCtl0: return intctl_[0];
    case kCp6IntCtl1: return intctl_[1];
    case kCp6IntCtl2: return intctl_[2];
    case kCp6IntCtl3: return intctl_[3];
    case kCp6Timer0Control: return timer_[0].control;
    case kCp6Timer1Control: return timer_[1].control;
    case kCp6Timer0Counter: return timer_[0].counter;
    case kCp6Timer1Counter: return timer_[1].counter;
    case kCp6Timer0Reload: return timer_[0].reload;
    case kCp6Timer1Reload: return timer_[1].reload;
    case kCp6TimerStatus: return tisr_;
    case kCp6WatchdogCtrl: return wdtcr_;
    case kCp6WatchdogStat: return wdtsr_;
    default: emu_.Get<Fatal>().Die("IOP13xx CP6 unsupported read key 0x%04X", key);
    }
}

void Iop13xxCp6::WriteRegisterLocked(uint32_t key, uint32_t value, uint32_t cycles_now) {
    AdvanceTimersLocked(cycles_now);
    switch (key) {
    case kCp6IntBase: intbase_ = value; break;
    case kCp6IntSize: intsize_ = value & 3u; break;
    case kCp6IntCtl0: {
        const uint32_t old = intctl_[0];
        intctl_[0] = value;
        if ((old ^ value) & (kMp377PowerFailIrqMask | (1u << 24))) {
            LOG(Caution,
                "IOP13xx CP6 IRQ mask: INTCTL0 old=0x%08X new=0x%08X "
                "src24=%u src31=%u pending0=0x%08X\n",
                old, value, (value >> 24) & 1u, (value >> kMp377PowerFailIrqSource) & 1u, pending_[0]);
        }
        break;
    }
    case kCp6IntCtl1: intctl_[1] = value; break;
    case kCp6IntCtl2: intctl_[2] = value; break;
    case kCp6IntCtl3: intctl_[3] = value; break;
    case kCp6IntStr0: intstr_[0] = value; break;
    case kCp6IntStr1: intstr_[1] = value; break;
    case kCp6IntStr2: intstr_[2] = value; break;
    case kCp6IntStr3: intstr_[3] = value; break;
    case kCp6Timer0Control:
        timer_[0].control = value;
        timer_[0].base_cycles = cycles_now;
        break;
    case kCp6Timer1Control:
        timer_[1].control = value;
        timer_[1].base_cycles = cycles_now;
        break;
    case kCp6Timer0Counter:
        timer_[0].counter = value;
        timer_[0].base_cycles = cycles_now;
        break;
    case kCp6Timer1Counter:
        timer_[1].counter = value;
        timer_[1].base_cycles = cycles_now;
        break;
    case kCp6Timer0Reload: timer_[0].reload = value; break;
    case kCp6Timer1Reload: timer_[1].reload = value; break;
    case kCp6TimerStatus: tisr_ &= ~value; break;
    case kCp6WatchdogCtrl: wdtcr_ = value; break;
    case kCp6WatchdogStat: wdtsr_ &= ~value; break;
    default: emu_.Get<Fatal>().Die("IOP13xx CP6 unsupported write key 0x%04X value 0x%08X", key, value);
    }
    AdvanceTimersLocked(cycles_now);
    NotifyLocked();
}

uint32_t __fastcall Iop13xxCp6::ReadHelper(Iop13xxCp6* self, uint32_t key) {
    std::lock_guard<std::mutex> guard(self->state_mutex_);
    return self->ReadRegisterLocked(key, self->TimerTicks());
}

void __fastcall Iop13xxCp6::WriteHelper(Iop13xxCp6* self, uint32_t key, uint32_t value) {
    std::lock_guard<std::mutex> guard(self->state_mutex_);
    self->WriteRegisterLocked(key, value, self->TimerTicks());
}

uint8_t* Iop13xxCp6::EmitRegisterTransfer(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    if (d->cp_opc != 0 || d->cp != 0 || d->rd == 15) {
        return EmitCoprocUnimplementedFatal(cursor, d, ctx);
    }

    const uint32_t key = Cp6Key(d->crn, d->crm, d->cp, d->cp_opc);
    const int32_t rd_disp = static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);
    EmitMovRegImm32(cursor, kEcx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)));
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

void Iop13xxCp6::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> guard(state_mutex_);
    AdvanceTimersLocked(TimerTicks());
    w.WriteBytes(intctl_, sizeof(intctl_));
    w.WriteBytes(intstr_, sizeof(intstr_));
    w.WriteBytes(pending_, sizeof(pending_));
    w.Write(intbase_);
    w.Write(intsize_);
    w.Write(tisr_);
    w.Write(wdtcr_);
    w.Write(wdtsr_);
    w.WriteBytes(timer_, sizeof(timer_));
}

void Iop13xxCp6::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> guard(state_mutex_);
    r.ReadBytes(intctl_, sizeof(intctl_));
    r.ReadBytes(intstr_, sizeof(intstr_));
    r.ReadBytes(pending_, sizeof(pending_));
    r.Read(intbase_);
    r.Read(intsize_);
    r.Read(tisr_);
    r.Read(wdtcr_);
    r.Read(wdtsr_);
    r.ReadBytes(timer_, sizeof(timer_));
}

void Iop13xxCp6::PostRestoreState() {
    std::lock_guard<std::mutex> guard(state_mutex_);
    const uint32_t ticks = TimerTicks();
    timer_[0].base_cycles = ticks;
    timer_[1].base_cycles = ticks;
    NotifyLocked();
}

REGISTER_SERVICE_AS(Iop13xxCp6, IrqController);
