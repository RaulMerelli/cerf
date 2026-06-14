#include "../freescale_epit_impl.h"

#include "../irq_controller.h"

namespace {

/* EPIT2 @ 0x73FB0000; TZIC source 41 (MCIMX51RM Table 3-2, ARM Domain
   Interrupt Summary). */
class Imx51Epit2
    : public cerf_freescale_epit_detail::FreescaleEpitBase<0x73FB0000u, SocFamily::iMX51> {
    using FreescaleEpitBase::FreescaleEpitBase;
    void AssertIrqLine()   override { emu_.Get<IrqController>().AssertIrq(41); }
    void DeassertIrqLine() override { emu_.Get<IrqController>().DeAssertIrq(41); }
};

}  /* namespace */

REGISTER_SERVICE(Imx51Epit2);
