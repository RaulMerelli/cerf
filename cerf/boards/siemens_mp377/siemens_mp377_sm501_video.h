#pragma once

#include "../../core/service.h"

#include <cstdint>

namespace siemens_mp377 {

class SiemensMp377Sm501Video : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    const uint8_t* Vram();
    bool WasWritten();

    bool WriteVramByte(uint32_t offset, uint8_t value);
    bool WriteVramHalf(uint32_t offset, uint16_t value);
    bool WriteVramWord(uint32_t offset, uint32_t value);

    uint32_t PanelFbOffset();
    uint32_t PanelPitchBytes();
    uint32_t PanelWidth();
    uint32_t PanelHeight();
};

} // namespace siemens_mp377
