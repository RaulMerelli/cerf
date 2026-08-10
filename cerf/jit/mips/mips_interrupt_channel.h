#pragma once

#include <atomic>
#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"

struct MipsCpuState;

class MipsInterruptChannel : public Service {
public:
    using Service::Service;
    ~MipsInterruptChannel() override;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    void SetExternalInterruptLevel(uint32_t ip_mask);
    void SignalIdleWake();

    uint32_t Level() const {
        return external_ip_.load(std::memory_order_acquire);
    }
    uint32_t DeviceIpMask() const { return device_ip_mask_; }

    static void __fastcall WaitHelper(MipsInterruptChannel* channel);

private:
    void*                 idle_event_ = nullptr;
    std::atomic<uint32_t> external_ip_{0};
    uint32_t              device_ip_mask_ = 0;

    MipsCpuState* cpu_state_ = nullptr;
};
