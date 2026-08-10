#pragma once

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"

struct MipsCpuState;

class MipsWideArithmetic : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    static void __fastcall DmultuHelper(uint32_t rs, uint32_t rt,
                                        MipsWideArithmetic* w);
    static void __fastcall DmultHelper(uint32_t rs, uint32_t rt,
                                       MipsWideArithmetic* w);
    static void __fastcall DdivHelper(uint32_t rs, uint32_t rt,
                                      MipsWideArithmetic* w);
    static void __fastcall DdivuHelper(uint32_t rs, uint32_t rt,
                                       MipsWideArithmetic* w);

    static void __fastcall DsllvHelper(uint32_t rd, uint32_t rt, uint32_t rs,
                                       MipsWideArithmetic* w);
    static void __fastcall DsrlvHelper(uint32_t rd, uint32_t rt, uint32_t rs,
                                       MipsWideArithmetic* w);
    static void __fastcall DsravHelper(uint32_t rd, uint32_t rt, uint32_t rs,
                                       MipsWideArithmetic* w);

private:
    MipsCpuState* cpu_state_ = nullptr;
};
