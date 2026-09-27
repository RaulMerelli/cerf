#include "../../cpu/arm_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "s3c2410_id.h"
#include "s3c2410_clocks.h"

namespace {

class S3C2410ArmProcessorConfig : public ArmProcessorConfig {
public:
    using ArmProcessorConfig::ArmProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::S3c2410;
    }

    /* S3C2410A User Manual, ARM Instruction Set, Block Data Transfer
       (p. 3-40): "Whenever R15 is stored to memory the stored value is
       the address of the STM instruction plus 12." */
    uint32_t PcStoreOffset()              const override { return 12; }
    bool     BaseRestoredAbortModel()     const override { return true; }
    uint32_t CacheLineSize()              const override { return 32; }
    uint32_t Midr()                       const override { return 0x69059201u; }
    uint32_t Ctr()                        const override { return 0x0B172172u; }
    bool     HasDsp()                     const override { return true; }
    bool     HasLoadStoreDouble()         const override { return true; }

    uint32_t CpuClockHz() const override {
        return static_cast<uint32_t>(emu_.Get<S3C2410Clocks>().CoreClockHz());
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(S3C2410ArmProcessorConfig, ArmProcessorConfig);
