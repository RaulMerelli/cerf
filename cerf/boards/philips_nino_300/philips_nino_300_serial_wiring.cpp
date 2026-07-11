#include "../../socs/pr31x00/pr31x00_serial_wiring.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"

namespace {

/* UART A ($0B0) is the docking-cradle COM1 serial.dll opens as DeviceArrayIndex 0
   (repllog/ActiveSync, rnaapp dial-up); UART B ($0C8) is the IrDA link, not an
   attach target. CTS=MFIODIN6 DCD=MFIODIN15 DTR=MFIOOUT13 RTS=MFIOOUT1 (serial.dll
   GetModemStatus sub_1861A68 + SET_DTR sub_186274C / SET_RTS sub_1862818). */
constexpr uint32_t kUartA = 0x10C000B0u;

class PhilipsNino300SerialWiring : public Pr31x00SerialWiring {
public:
    using Pr31x00SerialWiring::Pr31x00SerialWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsNino300;
    }

    std::optional<Pr31x00SerialModem> ForUart(uint32_t mmio_base) const override {
        if (mmio_base != kUartA) return std::nullopt;
        Pr31x00SerialModem m;
        m.label = L"COM1 (cradle)";
        m.cts   = Pr31x00SerialPin::OnMfio(6);
        m.dcd   = Pr31x00SerialPin::OnMfio(15);
        m.dtr   = Pr31x00SerialPin::OnMfio(13);
        m.rts   = Pr31x00SerialPin::OnMfio(1);
        return m;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsNino300SerialWiring, Pr31x00SerialWiring);
