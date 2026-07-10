#include "ucb1x00_codec.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* NetBSD ucb1200reg.h:226 UCB1200_ID 0x1004 (version 4, device 0, supplier 1). */
constexpr uint16_t kIdUcb1200 = 0x1004u;

class Ucb1200 : public Ucb1x00Codec {
public:
    using Ucb1x00Codec::Ucb1x00Codec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const Board b = bd->GetBoard();
        return b == Board::Jornada820 || b == Board::PhilipsNino300;
    }

protected:
    uint16_t DeviceId() const override { return kIdUcb1200; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Ucb1200, Ucb1x00Codec);
