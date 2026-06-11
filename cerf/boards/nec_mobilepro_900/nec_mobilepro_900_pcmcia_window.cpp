#include "nec_mobilepro_900_board_window.h"

#include "../../core/cerf_emulator.h"

namespace {

/* PCMCIA socket-controller register windows (pcmcia.dll slots 29/30). Two
   separate 1 MB scaffolds, NOT one window spanning both: the 16 MB gap between
   0x09000000 and 0x0A000000 must keep FATALing so a real device in it isn't
   silently masked by a returns-0 scaffold. */
class NecMobilePro900PcmciaWindow09 : public NecMobilePro900BoardWindow {
public:
    using NecMobilePro900BoardWindow::NecMobilePro900BoardWindow;
    uint32_t MmioBase() const override { return 0x09000000u; }
protected:
    const char* WindowName() const override { return "pcmcia-ctrl@0x09000000"; }
};

class NecMobilePro900PcmciaWindow0A : public NecMobilePro900BoardWindow {
public:
    using NecMobilePro900BoardWindow::NecMobilePro900BoardWindow;
    uint32_t MmioBase() const override { return 0x0A000000u; }
protected:
    const char* WindowName() const override { return "pcmcia-ctrl@0x0A000000"; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro900PcmciaWindow09);
REGISTER_SERVICE(NecMobilePro900PcmciaWindow0A);
