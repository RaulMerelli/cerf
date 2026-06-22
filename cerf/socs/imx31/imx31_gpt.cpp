#include "../freescale_gpt_impl.h"

#include "imx31_avic.h"

namespace {

/* GPT @ 0x53F90000; AVIC source 29 (MCIMX31RM §2.2 Table 2-3, p190). */
class Imx31Gpt
    : public cerf_freescale_gpt_detail::FreescaleGptBase<0x53F90000u, SocFamily::iMX31> {
    using FreescaleGptBase::FreescaleGptBase;
    void AssertIrqLine()   override { emu_.Get<Imx31Avic>().AssertSource(29u); }
    void DeassertIrqLine() override { emu_.Get<Imx31Avic>().DeassertSource(29u); }
};

}  /* namespace */

REGISTER_SERVICE(Imx31Gpt);
