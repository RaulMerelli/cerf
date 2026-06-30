#include "cortex_a8_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

class Omap3530ProcessorConfig : public CortexA8ProcessorConfigBase {
public:
    using CortexA8ProcessorConfigBase::CortexA8ProcessorConfigBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::OMAP3530;
    }

    uint32_t Midr() const override { return 0x410fc080u; }

    /* floor(720 000 000 / 32 768) - Cortex-A8 nominal max MPU clock over
       GPTIMER1 32 kHz functional clock. Used by Omap3530Gptimer1 /
       Omap3530Synctimer to convert guest_cycle_counter → ticks. */
    uint32_t CpuToOscrDivider() const override { return 21972u; }

    /* 720 MHz Cortex-A8 max MPU clock per OMAP3530 TRM §1.4.1. */
    uint32_t CpuClockHz() const override { return 720000000u; }

    uint32_t Clidr() const override { return 0x0A000003u; }

    uint32_t Ccsidr(uint32_t csselr) const override {
        const uint32_t level = (csselr >> 1) & 0x7u;
        const uint32_t ind   =  csselr       & 0x1u;
        if (level == 0) {
            return ind ? 0x2007e01au   /* L1 I-cache, 16 KB */
                       : 0xe007e01au;  /* L1 D-cache, 16 KB */
        }
        if (level == 1) {
            return 0xf0000000u;
        }
        return 0u;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Omap3530ProcessorConfig, ArmProcessorConfig);
