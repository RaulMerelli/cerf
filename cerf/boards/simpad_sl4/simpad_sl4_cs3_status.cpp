#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

#include <cstdint>

namespace {

/* SIMpad CS3 read-only status register (nCS3 @ PA 0x18000000). The PCMCIA socket
   GetStatus (pcmcia.dll sub_12F2590) reads bit0/bit1 as card-sense only when a
   card is present; presence is GPIO24 (CF_CD), which OnReady drives high for an
   empty socket. */
class SimpadSl4Cs3Status : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        /* No CF card inserted: GPIO24 (CF_CD) idles high so sub_12F2590 reports
           an empty socket (v5 & (1<<24) != 0 -> status 0). */
        emu_.Get<Sa11xxGpio>().DriveInputPin(24u, true);
    }

    uint32_t MmioBase() const override { return 0x18000000u; }
    uint32_t MmioSize() const override { return 0x00000080u; }  /* PDD maps 128 B */

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
};

}  /* namespace */

REGISTER_SERVICE(SimpadSl4Cs3Status);
