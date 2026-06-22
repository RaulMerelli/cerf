#include "../freescale_gpt_impl.h"

#include "../irq_controller.h"

namespace {

/* GPT @ 0x73FA0000; TZIC source 39 (MCIMX51RM Table 3-2, ARM Domain Interrupt
   Summary - also the source SBOOT registers for the GPT in sub_8005CBD4). */
class Imx51Gpt
    : public cerf_freescale_gpt_detail::FreescaleGptBase<0x73FA0000u, SocFamily::iMX51> {
    using FreescaleGptBase::FreescaleGptBase;
    void AssertIrqLine()   override { emu_.Get<IrqController>().AssertIrq(39); }
    void DeassertIrqLine() override { emu_.Get<IrqController>().DeAssertIrq(39); }
};

}  /* namespace */

REGISTER_SERVICE(Imx51Gpt);
