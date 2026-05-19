#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Sdma : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48056000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

protected:
    const char* Label() const override { return "SDMA"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "DMA4_REVISION";
        case 0x08: return "DMA4_IRQSTATUS_L0";
        case 0x0C: return "DMA4_IRQSTATUS_L1";
        case 0x10: return "DMA4_IRQSTATUS_L2";
        case 0x14: return "DMA4_IRQSTATUS_L3";
        case 0x18: return "DMA4_IRQENABLE_L0";
        case 0x1C: return "DMA4_IRQENABLE_L1";
        case 0x20: return "DMA4_IRQENABLE_L2";
        case 0x24: return "DMA4_IRQENABLE_L3";
        case 0x28: return "DMA4_SYSSTATUS";
        case 0x2C: return "DMA4_OCP_SYSCONFIG";
        case 0x40: return "DMA4_CAPS_0";
        case 0x44: return "DMA4_CAPS_1";
        case 0x48: return "DMA4_CAPS_2";
        case 0x4C: return "DMA4_CAPS_3";
        case 0x50: return "DMA4_CAPS_4";
        case 0x60: return "DMA4_GCR";
        case 0x78: return "DMA4_GCR_AGAIN";
        }
        /* Per-channel registers at 0x80 + n*0x60 + sub-offset (32 channels). */
        if (off >= 0x80u && off < 0x80u + 32u * 0x60u) {
            return "DMA4_CHANNEL_REG";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Sdma);
