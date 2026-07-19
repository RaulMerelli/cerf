#include "../vr41xx/vr41xx_led_impl.h"

#include <cstdint>

namespace {

using cerf_vr41xx_led_detail::Vr41xxLedBase;

/* VR4102 LED at 0x0B000240 (UM Table 23-1, p.453). */
class Vr4102Led : public Vr41xxLedBase<SocFamily::VR4102, 0x0B000240u> {
public:
    using Vr41xxLedBase::Vr41xxLedBase;

protected:
    /* leddrv init zeroes the reserved 0x0B000244/0x0B000246 gap between LEDLTSREG
       (0x0B000242) and LEDCNTREG (0x0B000248). */
    void WriteHalfExt(uint32_t addr, uint16_t value) override {
        switch (addr - MmioBase()) {
            case 0x04u: case 0x06u: return;
            default: Vr41xxLedBase::WriteHalfExt(addr, value); return;
        }
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Led);
