#pragma once

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"

struct ArmCpuState;

class ArmCpu;
class ArmMmu;
class ArmPageWalker;

class ArmExceptionFrame : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
    }

    static uint32_t __fastcall RfeHelper(uint32_t rn_value, uint32_t encoded,
                                         ArmExceptionFrame* frame);

    static uint32_t __fastcall SrsHelper(uint32_t encoded,
                                         ArmExceptionFrame* frame,
                                         uint32_t guest_pc);

private:
    ArmCpu*        cpu_       = nullptr;
    ArmMmu*        mmu_       = nullptr;
    ArmPageWalker* walker_    = nullptr;
    ArmCpuState*   cpu_state_ = nullptr;
};
