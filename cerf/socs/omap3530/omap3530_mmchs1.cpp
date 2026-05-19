#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Mmchs1 : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x4809C000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

protected:
    const char* Label() const override { return "MMCHS1"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x010: return "MMCHS_SYSCONFIG";
        case 0x014: return "MMCHS_SYSSTATUS";
        case 0x024: return "MMCHS_CSRE";
        case 0x028: return "MMCHS_SYSTEST";
        case 0x02C: return "MMCHS_CON";
        case 0x030: return "MMCHS_PWCNT";
        case 0x104: return "MMCHS_BLK";
        case 0x108: return "MMCHS_ARG";
        case 0x10C: return "MMCHS_CMD";
        case 0x110: return "MMCHS_RSP10";
        case 0x114: return "MMCHS_RSP32";
        case 0x118: return "MMCHS_RSP54";
        case 0x11C: return "MMCHS_RSP76";
        case 0x120: return "MMCHS_DATA";
        case 0x124: return "MMCHS_PSTATE";
        case 0x128: return "MMCHS_HCTL";
        case 0x12C: return "MMCHS_SYSCTL";
        case 0x130: return "MMCHS_STAT";
        case 0x134: return "MMCHS_IE";
        case 0x138: return "MMCHS_ISE";
        case 0x13C: return "MMCHS_AC12";
        case 0x140: return "MMCHS_CAPA";
        case 0x148: return "MMCHS_CUR_CAPA";
        case 0x1FC: return "MMCHS_REV";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Mmchs1);
