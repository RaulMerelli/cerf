#include "xscale_processor_config_base.h"

namespace {
class Iop13xxProcessorConfig final : public XscaleProcessorConfigBase {
public:
    using XscaleProcessorConfigBase::XscaleProcessorConfigBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::IOP13xx;
    }
    uint32_t Midr() const override { return 0x69056000u; }
    uint32_t Ctr() const override { return 0x0B192192u; }
    uint32_t CpuClockHz() const override { return 800000000u; }
    uint32_t CpuToOscrDivider() const override { return 32u; }
    uint32_t CpuToHighfreqClockDivider() const override { return 32u; }
    uint32_t CpuToLowfreqClockDivider() const override { return 24414u; }

};
} // namespace
REGISTER_SERVICE_AS(Iop13xxProcessorConfig, ArmProcessorConfig);
