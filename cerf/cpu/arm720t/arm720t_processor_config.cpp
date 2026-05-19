#include "../arm_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"

namespace {

class Arm720TProcessorConfig : public ArmProcessorConfig {
public:
    using ArmProcessorConfig::ArmProcessorConfig;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::OdoArm720;
    }

    uint32_t PcStoreOffset()              const override { return 12; }
    bool     BaseRestoredAbortModel()     const override { return false; }
    bool     MemoryBeforeWritebackModel() const override { return true; }
    bool     GenerateSyscalls()           const override { return false; }
    uint32_t CacheLineSize()              const override { return 32; }
    uint32_t Midr()                       const override { return 0x41807200u; }
    uint32_t Ctr()                        const override { return 0x41807200u; }

    bool     HasDsp()                     const override { return false; }
    bool     HasLoadStoreDouble()         const override { return false; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Arm720TProcessorConfig, ArmProcessorConfig);
