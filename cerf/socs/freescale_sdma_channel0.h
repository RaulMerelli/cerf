#pragma once

#include "../core/service.h"
#include "freescale_sdma_regs.h"

#include <cstdint>

class StateReader;
class StateWriter;

class FreescaleSdmaChannel0 : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void Execute(uint32_t mode, uint32_t arm_src_pa,
                 uint32_t sdma_dst_word, uint32_t sdma_mmio_base);
    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);
    void Reset();

    uint32_t CurrentAddress() const { return current_address_; }

private:
    bool context_loaded_[cerf_freescale_sdma_detail::kChannelCount] = {};
    uint32_t current_address_ = 0;
    uint16_t program_[cerf_freescale_sdma_detail::kSdmaProgramWords] = {};
    uint32_t data_[cerf_freescale_sdma_detail::kSdmaDataWords] = {};
};
