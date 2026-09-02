#pragma once

#include <cstdint>
#include <optional>

#include "../../core/service.h"
#include "arm_mmu_state.h"

class ArmProcessorConfig;
class EmulatedMemory;

class ArmMmuProbe : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override;

    uint8_t* PeekVaToHost(uint32_t va);
    bool     PeekVaToPa(uint32_t va, uint32_t* pa);

    /* PA and PAR memory attributes of the entry the last translation of
       `va` left in the data TLB (ARM DDI 0406C.d B4.1.112). */
    [[nodiscard]] bool TlbPar(uint32_t va, uint32_t* pa, uint16_t* attrs) const;

    /* Same, from the guest page tables, for a translation that left no TLB
       entry behind. */
    [[nodiscard]] bool WalkPar(uint32_t va, uint32_t* pa, uint16_t* attrs) const;

    std::optional<uint8_t*> PeekDataTlb(uint32_t va) const;

    bool ExecPageGlobal(uint32_t folded_va) const;

private:
    std::optional<uint32_t> WalkVaToPa(uint32_t va);

    const ArmTlbEntry* MatchDataTlb(uint32_t va, uint32_t* folded) const;

    ArmMmuState*        state_p_          = nullptr;
    EmulatedMemory*     memory_           = nullptr;
    ArmProcessorConfig* processor_config_ = nullptr;
};
