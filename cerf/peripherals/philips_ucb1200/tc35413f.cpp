#include "ucb1x00_codec.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* NetBSD ucb1200reg.h:230 TC35413F_ID 0x9712 (TOSHIBA); ucb1200.c:95 ucb_id[]
   drives the TC35413F with the UCB1200 register map. */
constexpr uint16_t kIdTc35413f = 0x9712u;

class Tc35413f : public Ucb1x00Codec {
public:
    using Ucb1x00Codec::Ucb1x00Codec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
    }

protected:
    uint16_t DeviceId() const override { return kIdTc35413f; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Tc35413f, Ucb1x00Codec);
