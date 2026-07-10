#include "../../peripherals/philips_ucb1200/ucb1x00_board.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* The Jornada 820's touch panel hangs off the nCS3 ASIC, not the codec, so no
   plate is driven and every converted channel reads mid-scale. */
constexpr uint16_t kNominalSample = 0x0200u;

class Jornada820UcbBoard : public Ucb1x00Board {
public:
    using Ucb1x00Board::Ucb1x00Board;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

    uint16_t AuxAdc(uint8_t) override { return kNominalSample; }

    bool     TouchDown() const override { return false; }
    uint16_t TouchAdcX() override { return kNominalSample; }
    uint16_t TouchAdcY() override { return kNominalSample; }
    uint16_t TouchAdcPressure() override { return kNominalSample; }

    uint16_t IoData(uint16_t driven) override { return driven; }

    uint16_t PenIrqStatus() override { return 0; }
    void     ClearPenIrq(uint16_t) override {}
    void     SetPenIrqArmed(uint16_t) override {}
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820UcbBoard, Ucb1x00Board);
