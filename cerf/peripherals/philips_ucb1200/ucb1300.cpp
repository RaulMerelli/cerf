#include "ucb1x00_codec.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* Linux ucb1x00.h UCB_ID_1300 0x1005, which is what the SIMpad's touch.dll
   matches. NetBSD ucb1200reg.h:228 records 0x100a for the same part. */
constexpr uint16_t kIdUcb1300 = 0x1005u;

class Ucb1300 : public Ucb1x00Codec {
public:
    using Ucb1x00Codec::Ucb1x00Codec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

protected:
    uint16_t DeviceId() const override { return kIdUcb1300; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Ucb1300, Ucb1x00Codec);
