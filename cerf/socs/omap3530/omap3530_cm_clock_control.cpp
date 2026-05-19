#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmClockControl : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004D00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_CLOCK_CONTROL"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_CLKEN_PLL";
        case 0x04: return "CM_CLKEN2_PLL";
        case 0x20: return "CM_IDLEST_CKGEN";
        case 0x24: return "CM_IDLEST2_CKGEN";
        case 0x30: return "CM_AUTOIDLE_PLL";
        case 0x34: return "CM_AUTOIDLE2_PLL";
        case 0x40: return "CM_CLKSEL1_PLL";
        case 0x44: return "CM_CLKSEL2_PLL";
        case 0x48: return "CM_CLKSEL3_PLL";
        case 0x4C: return "CM_CLKSEL4_PLL";
        case 0x50: return "CM_CLKSEL5_PLL";
        case 0x70: return "CM_CLKOUT_CTRL";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmClockControl);
