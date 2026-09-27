#include "../../socs/s3c2410/s3c2410_clock_input.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "siemens_p177_id.h"

namespace {

/* siemens_tp177b_4inch_v1020 nk.exe sub_8303EFC8: the OAL derives FCLK
   from MPLLCON with a 12,000,000 Hz input. */
class SiemensP177ClockInput : public S3C2410ClockInput {
public:
    using S3C2410ClockInput::S3C2410ClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardId() == BoardId::SiemensP177;
    }

    uint64_t FinHz() const override { return 12000000u; }
};

}

REGISTER_SERVICE_AS(SiemensP177ClockInput, S3C2410ClockInput);
