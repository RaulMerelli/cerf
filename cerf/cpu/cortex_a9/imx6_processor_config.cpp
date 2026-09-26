#include "cortex_a9_processor_config.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

namespace {
class Imx6ProcessorConfig final : public CortexA9ProcessorConfigBase {
public:
    using CortexA9ProcessorConfigBase::CortexA9ProcessorConfigBase;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    /* ARM DDI 0388I Table 4-28; IMX6DQIEC Rev.6 Table 2: Cortex-A9 r2p10;
       hmi_ktp400_mobile_v17 nk.exe 0x80310324. */
    uint32_t Midr() const override { return 0x412FC09Au; }
    /* IMX6DQIEC Rev.6 Figure 1: frequency code 08 is 800 MHz, industrial grade; hmi_ktp400_mobile_v13 nk.exe +0x187D0. */
    uint32_t CpuClockHz() const override { return 800000000u; }
    /* IMX6DQRM Rev.2 section 18.5.1.1.1. */
    uint32_t CpuToOscrDivider() const override { return 33u; }
    uint32_t CpuToHighfreqClockDivider() const override { return 33u; }
    /* IMX6DQRM Rev.2 section 18.5.2. */
    uint32_t CpuToLowfreqClockDivider() const override { return 24414u; }
    /* ARM DDI 0388I Table 4-33. */
    uint32_t Clidr() const override { return 0x09000003u; }
    uint32_t Ccsidr(uint32_t csselr) const override {
        if (((csselr >> 1) & 7u) != 0u) return 0u;
        /* ARM DDI 0388I Table 4-32; IMX6DQIEC Rev.6 section 1.2 specifies 32 KByte L1 caches. */
        return (csselr & 1u) ? 0x201FE019u : 0x701FE019u;
    }
};
}
REGISTER_SERVICE_AS(Imx6ProcessorConfig, ArmProcessorConfig);
