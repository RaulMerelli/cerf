#pragma once

#include "../ac97_codec.h"

#include <cstdint>

/* Wolfson WM9705 AC'97 audio + touch + battery codec (Falcon 4220 / Askey PC3xx).
   Runtime touch coordinates stream over AC-link slot 5 to the controller's MODR
   FIFO (Pxa255Ac97); the codec registers here serve device identity, the battery
   ADC poll, and touch.dll's init X/Y panel-probe. */
class Wm9705Codec : public Ac97Codec {
public:
    using Ac97Codec::Ac97Codec;

    bool ShouldRegister() override;
    void OnReady() override;

    uint16_t ReadReg(uint32_t reg) override;
    void     WriteReg(uint32_t reg, uint16_t value) override;

private:
    static constexpr uint32_t kNumRegs = 512u;  /* controller codec window 0x200..0x600. */
    uint16_t reg_[kNumRegs] = {};
};
