#include "nec_mobilepro_900_board_window.h"

#include "../../core/cerf_emulator.h"

namespace {

/* Static CS2 board device (PA 0x08000000, OAT VA 0x88000000) - trueffs.dll probes
   it for a DiskOnChip flash; returns-0 makes the probe find no signature. */
class NecMobilePro900Cs2Window : public NecMobilePro900BoardWindow {
public:
    using NecMobilePro900BoardWindow::NecMobilePro900BoardWindow;
    uint32_t MmioBase() const override { return 0x08000000u; }
protected:
    const char* WindowName() const override { return "cs2-dev@0x08000000"; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro900Cs2Window);
