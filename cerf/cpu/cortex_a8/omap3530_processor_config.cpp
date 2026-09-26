#include "cortex_a8_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../boards/board_context.h"
#include "../../socs/omap3530/omap3530_cm_mpu.h"
#include "../../socs/omap3530/omap3530_id.h"

#include <cstdint>

namespace {

class Omap3530ProcessorConfig : public CortexA8ProcessorConfigBase {
public:
    using CortexA8ProcessorConfigBase::CortexA8ProcessorConfigBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Omap3530;
    }

    uint32_t Midr() const override { return 0x410fc080u; }

    uint32_t CpuClockHz() const override {
        const uint64_t hz = emu_.Get<Omap3530CmMpu>().ArmFclkHz();
        if (hz > UINT32_MAX) {
            emu_.Get<Fatal>().Die("omap3530: the DPLL1 ARM_FCLK of %llu Hz does not fit the "
                                  "32-bit CPU clock rate",
                                  static_cast<unsigned long long>(hz));
        }
        return static_cast<uint32_t>(hz);
    }

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
