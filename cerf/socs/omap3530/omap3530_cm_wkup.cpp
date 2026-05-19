#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmWkup : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004C00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_WKUP"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_FCLKEN_WKUP";
        case 0x10: return "CM_ICLKEN_WKUP";
        case 0x20: return "CM_IDLEST_WKUP";
        case 0x30: return "CM_AUTOIDLE_WKUP";
        case 0x40: return "CM_CLKSEL_WKUP";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmWkup);
