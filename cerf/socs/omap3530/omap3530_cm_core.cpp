#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmCore : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004A00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    /* CM_CLKSTST_<DOMAIN> derived from CM_CLKSTCTRL_<DOMAIN>: each
       2-bit transition-control field (NO_SLEEP/SW_SLEEP/SW_WAKEUP/
       HW_AUTO = 0/1/2/3) maps to one CLKACTIVITY bit; field != 1
       means clock is active. */
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x4Cu) {
            const uint32_t ctrl = PeekReg(0x48u);
            uint32_t st = 0;
            for (uint32_t i = 0; i < 16u; ++i) {
                if (((ctrl >> (i * 2u)) & 0x3u) != 1u) {
                    st |= 1u << i;
                }
            }
            return st;
        }
        return Omap3530PrcmStubBlock::ReadWord(addr);
    }

protected:
    const char* Label() const override { return "CM_CORE"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_FCLKEN1_CORE";
        case 0x08: return "CM_FCLKEN3_CORE";
        case 0x10: return "CM_ICLKEN1_CORE";
        case 0x14: return "CM_ICLKEN2_CORE";
        case 0x18: return "CM_ICLKEN3_CORE";
        case 0x20: return "CM_IDLEST1_CORE";
        case 0x24: return "CM_IDLEST2_CORE";
        case 0x28: return "CM_IDLEST3_CORE";
        case 0x30: return "CM_AUTOIDLE1_CORE";
        case 0x34: return "CM_AUTOIDLE2_CORE";
        case 0x38: return "CM_AUTOIDLE3_CORE";
        case 0x40: return "CM_CLKSEL_CORE";
        case 0x48: return "CM_CLKSTCTRL_CORE";
        case 0x4C: return "CM_CLKSTST_CORE";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmCore);
