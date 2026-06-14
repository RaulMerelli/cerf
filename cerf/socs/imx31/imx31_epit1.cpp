#include "../freescale_epit_impl.h"

#include "imx31_avic.h"

namespace {

/* EPIT1 @ 0x53F94000; AVIC source 28 (MCIMX31RM Table 2-3, p190). */
class Imx31Epit1
    : public cerf_freescale_epit_detail::FreescaleEpitBase<0x53F94000u, SocFamily::iMX31> {
    using FreescaleEpitBase::FreescaleEpitBase;
    void AssertIrqLine()   override { emu_.Get<Imx31Avic>().AssertSource(28u); }
    void DeassertIrqLine() override { emu_.Get<Imx31Avic>().DeassertSource(28u); }
};

}  /* namespace */

REGISTER_SERVICE(Imx31Epit1);
