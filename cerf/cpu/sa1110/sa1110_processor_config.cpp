#include "../sa11xx/sa11xx_processor_config_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../socs/sa11xx/sa1110_id.h"

namespace {

class Sa1110ProcessorConfig : public Sa11xxProcessorConfigBase {
public:
    using Sa11xxProcessorConfigBase::Sa11xxProcessorConfigBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Sa1110;
    }

    /* SA-1110 Dev Manual §5.2.1 (Arch 01 = v4, Part B11 = SA1110);
       proc-sa1100.S:277 sa1110 = 0x6901b110 mask 0xfffffff0. */
    uint32_t Midr() const override { return 0x6901B110u; }
    uint32_t Ctr()  const override { return 0x6901B110u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Sa1110ProcessorConfig, ArmProcessorConfig);
