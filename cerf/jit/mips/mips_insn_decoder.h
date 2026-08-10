#pragma once

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "mips16_decoder.h"
#include "mips_block_context.h"
#include "mips_decoder.h"

struct MipsCpuState;

class EmulatedMemory;
class MipsMmu;
class MipsPlaceFnSelector;

class MipsInsnDecoder : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    void Decode(MipsBlockContext* ctx, uint32_t guest_pc);
    void Decode16(MipsBlockContext* ctx, uint32_t guest_pc);

private:
    bool Fetch16(MipsBlockContext* ctx, uint32_t va, uint16_t* hw, uint32_t* pa);

    MipsDecoder   decoder_;
    Mips16Decoder m16_decoder_;

    MipsCpuState*        cpu_state_ = nullptr;
    MipsMmu*             mmu_       = nullptr;
    EmulatedMemory*      memory_    = nullptr;
    MipsPlaceFnSelector* selector_  = nullptr;
};
