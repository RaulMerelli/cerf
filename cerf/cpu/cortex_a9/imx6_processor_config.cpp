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
    uint32_t Midr() const override { return 0x412FC09Au; }
    uint32_t CpuClockHz() const override { return 800000000u; }
    uint32_t CpuToOscrDivider() const override { return 33u; }
    uint32_t CpuToHighfreqClockDivider() const override { return 33u; }
    uint32_t CpuToLowfreqClockDivider() const override { return 24414u; }
    uint32_t Clidr() const override { return 0x09000003u; }
    uint32_t Ccsidr(uint32_t csselr) const override {
        if (((csselr >> 1) & 7u) != 0u) return 0u;
        return (csselr & 1u) ? 0x203FE019u : 0x700FE019u;
    }
};
} // namespace
REGISTER_SERVICE_AS(Imx6ProcessorConfig, ArmProcessorConfig);
