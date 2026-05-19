#include "../../cpu/arm_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"

namespace {

class S3C2410ArmProcessorConfig : public ArmProcessorConfig {
public:
    using ArmProcessorConfig::ArmProcessorConfig;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetSoc() == SocFamily::S3C2410;
    }

    uint32_t PcStoreOffset()              const override { return 12; }
    bool     BaseRestoredAbortModel()     const override { return true; }
    bool     MemoryBeforeWritebackModel() const override { return true; }
    bool     GenerateSyscalls()           const override { return false; }
    uint32_t CacheLineSize()              const override { return 32; }
    uint32_t Midr()                       const override { return 0x69059201u; }
    uint32_t Ctr()                        const override { return 0x0B172172u; }
    bool     HasDsp()                     const override { return true; }
    bool     HasLoadStoreDouble()         const override { return true; }
};

}  /* namespace */

REGISTER_SERVICE_AS(S3C2410ArmProcessorConfig, ArmProcessorConfig);
