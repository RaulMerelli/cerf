#include "nec_mobilepro_900_board_window.h"

#include "../../core/cerf_emulator.h"

namespace {

/* NEC MobilePro 900 (P530) board system-controller CPLD on static CS (PA
   0x13000000, OEMAddressTable VA 0x88D00000): nk.exe sub_9023B758 (tick ISR)
   plus the AdvBacklight/backlite/battery/contrast/keybddr/bs_serial drivers
   share this register block; per-register semantics are RE'd per driver. */
class NecMobilePro900BoardCpld : public NecMobilePro900BoardWindow {
public:
    using NecMobilePro900BoardWindow::NecMobilePro900BoardWindow;

    uint32_t MmioBase() const override { return 0x13000000u; }

protected:
    const char* WindowName() const override { return "sysctl-cpld@0x13000000"; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro900BoardCpld);
