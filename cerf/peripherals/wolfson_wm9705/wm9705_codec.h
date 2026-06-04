#pragma once

#include "../ac97_codec.h"

#include <cstdint>

/* Wolfson WM9705 AC'97 audio + touchscreen codec (Falcon 4220 / Askey PC3xx).
   Registers only — touch samples do NOT flow through here; in continuous mode
   the WM9705 streams them on AC-link slot 5 into the controller's modem-in
   FIFO (see Pxa255Ac97 MODR), not via a codec-register read. */
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
