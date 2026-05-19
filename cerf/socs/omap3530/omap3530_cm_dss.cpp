#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmDss : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004E00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_DSS"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_FCLKEN_DSS";
        case 0x10: return "CM_ICLKEN_DSS";
        case 0x20: return "CM_IDLEST_DSS";
        case 0x30: return "CM_AUTOIDLE_DSS";
        case 0x40: return "CM_CLKSEL_DSS";
        case 0x44: return "CM_SLEEPDEP_DSS";
        case 0x48: return "CM_CLKSTCTRL_DSS";
        case 0x4C: return "CM_CLKSTST_DSS";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmDss);
