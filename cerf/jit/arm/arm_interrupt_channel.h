#pragma once

#include <atomic>
#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"

struct ArmCpuState;

class ArmProcessorConfig;

class ArmInterruptChannel : public Service {
public:
    using Service::Service;
    ~ArmInterruptChannel() override;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
    }

    void SetInterruptPending();
    void ClearInterruptPending();

    uint32_t Level() const { return irq_line_.load(std::memory_order_acquire); }

    void Wake();

    static void __fastcall WfiHelper(ArmInterruptChannel* channel);

private:
    std::atomic<uint32_t> irq_line_{0};
    void*                 idle_event_ = nullptr;

    ArmCpuState*        cpu_state_        = nullptr;
    ArmProcessorConfig* processor_config_ = nullptr;
};
