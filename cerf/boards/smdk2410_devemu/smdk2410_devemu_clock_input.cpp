#include "../../socs/s3c2410/s3c2410_clock_input.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "devemu_id.h"

namespace {

/* S3C2410A UM p.7-20: the PLL value selection table is tabulated at Input
   Frequency 12.00MHz, and devemu_wm2003se nk.exe start's MPLLCON 0xA1031 /
   UPLLCON 0x48032 yield its 202.80MHz row and p.7-19's 48.00MHz UPLL. */
class Smdk2410DevEmuClockInput : public S3C2410ClockInput {
public:
    using S3C2410ClockInput::S3C2410ClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardId() == BoardId::Devemu;
    }

    uint64_t FinHz() const override { return 12000000u; }
};

}

REGISTER_SERVICE_AS(Smdk2410DevEmuClockInput, S3C2410ClockInput);
