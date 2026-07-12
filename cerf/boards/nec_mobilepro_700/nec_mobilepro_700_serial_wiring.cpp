#include "../../socs/vr4102/vr4102_serial_wiring.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"

namespace {

/* Carrier reaches the guest on GIU pin 5, asserted low: serial.dll HWGetModemStatus
   (sub_15810C4) sets MS_RLSD_ON when (*(WORD*)0x0B000104 & 0x20) == 0, and HWInit arms
   that pin's GIU interrupt. The SIU's fixed DCD# input PIOD[15] (UM 18.2.3) is unused on
   this board. */
class NecMobilePro700SerialWiring : public Vr4102SerialWiring {
public:
    using Vr4102SerialWiring::Vr4102SerialWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }

    std::optional<Vr4102SerialModem> ForSiu() const override {
        Vr4102SerialModem m;
        m.label       = L"COM1";
        m.dcd_giu_pin = 5;
        return m;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro700SerialWiring, Vr4102SerialWiring);
