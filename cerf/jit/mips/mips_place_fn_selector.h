#pragma once

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "mips_decoded_insn.h"

class MipsPlaceFnSelector : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    MipsPlaceFn Select(const MipsDecodedInsn* d);
};
