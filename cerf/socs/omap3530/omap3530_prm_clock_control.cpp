#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmClockControl : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48306D00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_CLOCK_CONTROL"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x40: return "PRM_CLKSEL";
        case 0x70: return "PRM_CLKOUT_CTRL";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmClockControl);
