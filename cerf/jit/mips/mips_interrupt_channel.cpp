#include "mips_interrupt_channel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/mips_processor_config.h"
#include "mips_cpu.h"
#include "mips_cpu_state.h"

REGISTER_SERVICE(MipsInterruptChannel);

MipsInterruptChannel::~MipsInterruptChannel() {
    if (idle_event_) {
        CloseHandle(idle_event_);
        idle_event_ = nullptr;
    }
}

void MipsInterruptChannel::OnReady() {
    cpu_state_      = emu_.Get<MipsCpu>().State();
    device_ip_mask_ = emu_.Get<MipsProcessorConfig>().DeviceIpMask();

    idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idle_event_) {
        LOG(Caution, "MipsInterruptChannel: CreateEventW(idle_event) failed "
                "gle=%lu\n", GetLastError());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void MipsInterruptChannel::SignalIdleWake() {
    if (idle_event_) SetEvent(idle_event_);
}

void MipsInterruptChannel::SetExternalInterruptLevel(uint32_t ip_mask) {
    const uint32_t prev = external_ip_.exchange(ip_mask, std::memory_order_acq_rel);
    if (ip_mask & ~prev) SignalIdleWake();
}

void __fastcall MipsInterruptChannel::WaitHelper(MipsInterruptChannel* channel) {
    MipsCpuState& s = *channel->cpu_state_;
    if (s.reset_pending || s.deep_sleep) return;
    /* Standby/Suspend halt the CPU core until any interrupt; RTC+ICU keep running
       (VR4102 UM ch.27 p643/p646, Table 15-3 p326). */
    if (channel->external_ip_.load(std::memory_order_acquire) &
        channel->device_ip_mask_) {
        return;
    }
    WaitForSingleObject(channel->idle_event_, 1);
}
