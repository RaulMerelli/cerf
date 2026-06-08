#include "../../peripherals/cirrus_pd6710/pd6710_management_irq_line.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/irq_controller.h"

namespace {

/* PD6710 -INTR is wired to EINT3 (BSP pcc_smdk2410.reg "CSCIrq":
   "The SMDK2410 board routes the PD6710 -INTR to EINT3"). EINT0-3 are
   direct SRCPND sources, not EINTPEND rollups, so the pulse goes
   straight to the interrupt controller. */
constexpr int kEint3SourceBit = 3;

class DevEmuPd6710ManagementIrqLine : public Pd6710ManagementIrqLine {
public:
    using Pd6710ManagementIrqLine::Pd6710ManagementIrqLine;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void Pulse() override {
        emu_.Get<IrqController>().AssertIrq(kEint3SourceBit);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(DevEmuPd6710ManagementIrqLine, Pd6710ManagementIrqLine);
