#include "../../peripherals/rtc8564/rtc8564_wiring.h"

#include "../board_context.h"
#include "siemens_mp377_id.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/irq_controller.h"

namespace {

class SiemensMp377Rtc8564Wiring final : public Rtc8564Wiring {
public:
    using Rtc8564Wiring::Rtc8564Wiring;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardId() == BoardId::SiemensMp377;
    }

    void SetInterrupt(bool active) override {
        if (active)
            emu_.Get<IrqController>().AssertIrq(0x22);
        else
            emu_.Get<IrqController>().DeAssertIrq(0x22);
    }

    int CalendarYearBase() const override { return 1980; }
};

REGISTER_SERVICE_AS(SiemensMp377Rtc8564Wiring, Rtc8564Wiring);

} // namespace
