#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmGlobal : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48307200u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_GLOBAL"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x20: return "PRM_VC_SMPS_SA";
        case 0x24: return "PRM_VC_SMPS_VOL_RA";
        case 0x28: return "PRM_VC_SMPS_CMD_RA";
        case 0x2C: return "PRM_VC_CMD_VAL_0";
        case 0x30: return "PRM_VC_CMD_VAL_1";
        case 0x34: return "PRM_VC_CH_CONF";
        case 0x38: return "PRM_VC_I2C_CFG";
        case 0x3C: return "PRM_VC_BYPASS_VAL";
        case 0x50: return "PRM_RSTCTRL";
        case 0x54: return "PRM_RSTTIME";
        case 0x58: return "PRM_RSTST";
        case 0x60: return "PRM_VOLTCTRL";
        case 0x64: return "PRM_SRAM_PCHARGE";
        case 0x70: return "PRM_CLKSRC_CTRL";
        case 0x80: return "PRM_OBS";
        case 0x90: return "PRM_VOLTSETUP1";
        case 0x94: return "PRM_VOLTOFFSET";
        case 0x98: return "PRM_CLKSETUP";
        case 0x9C: return "PRM_POLCTRL";
        case 0xA0: return "PRM_VOLTSETUP2";
        case 0xB0: return "PRM_VP1_CONFIG";
        case 0xB4: return "PRM_VP1_VSTEPMIN";
        case 0xB8: return "PRM_VP1_VSTEPMAX";
        case 0xBC: return "PRM_VP1_VLIMITTO";
        case 0xC0: return "PRM_VP1_VOLTAGE";
        case 0xC4: return "PRM_VP1_STATUS";
        case 0xD0: return "PRM_VP2_CONFIG";
        case 0xD4: return "PRM_VP2_VSTEPMIN";
        case 0xD8: return "PRM_VP2_VSTEPMAX";
        case 0xDC: return "PRM_VP2_VLIMITTO";
        case 0xE0: return "PRM_VP2_VOLTAGE";
        case 0xE4: return "PRM_VP2_STATUS";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmGlobal);
