#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* SIMpad CS3 read-only status register (nCS3 @ PA 0x18000000). PDCardSetAdapter
   (pcmcia.dll sub_12F2D90) reads bits 2:3 as the Vcc voltage sense ((x&0xC)==0xC
   -> 5V); 0xFFFF presents the 5V sense both catalog cards (NE2000, CF) use. */
class SimpadSl4Cs3Status : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x18000000u; }
    uint32_t MmioSize() const override { return 0x00000080u; }  /* PDD maps 128 B */

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
};

}  /* namespace */

REGISTER_SERVICE(SimpadSl4Cs3Status);
