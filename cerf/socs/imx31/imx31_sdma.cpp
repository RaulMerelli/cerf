#include "../freescale_sdma_impl.h"

#include "imx31_avic.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

using cerf_freescale_sdma_detail::FreescaleSdmaBase;

/* SDMA AP interrupt = AVIC source 34 (MCIMX31RM Table 2-3). */
constexpr uint32_t kAvicSourceSdma = 34u;
/* i.MX31 divergent registers (MCIMX31RM Table 40-10): EVT_MIRROR @0x54
   (read-only DMA-request mirror), CHNENBL @0x80, 32 events. */
constexpr uint32_t kOffEvtMirror   = 0x54u;
constexpr uint32_t kOffChnenblBase = 0x80u;
constexpr uint32_t kChnenblCount   = 32u;

class Imx31Sdma : public FreescaleSdmaBase<0x53FD4000u, SocFamily::iMX31> {
public:
    using FreescaleSdmaBase::FreescaleSdmaBase;

protected:
    void AssertIrqLine()   override { emu_.Get<Imx31Avic>().AssertSource(kAvicSourceSdma); }
    void DeassertIrqLine() override { emu_.Get<Imx31Avic>().DeassertSource(kAvicSourceSdma); }

    uint32_t ChnenblBase()  const override { return kOffChnenblBase; }
    uint32_t ChnenblCount() const override { return kChnenblCount; }

    bool ReadExtra(uint32_t off, uint32_t& out) override {
        if (off == kOffEvtMirror) { out = 0; return true; }   /* no DMA-request events */
        return false;
    }
};

}  /* namespace */

REGISTER_SERVICE(Imx31Sdma);
