#include "../freescale_sdma_impl.h"

#include "../irq_controller.h"
#include "imx51_uart1.h"
#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

using cerf_freescale_sdma_detail::FreescaleSdmaBase;

/* SDMA AP interrupt = TZIC source 6 (MCIMX51RM Table 3-2, ARM Domain Interrupt
   Summary). */
constexpr uint32_t kTzicSourceSdma = 6u;
/* i.MX51 divergent registers (MCIMX51RM Table 52-9): EVT_MIRROR @0x60 (read-only
   DMA-request mirror), CHNENBL @0x200, 48 events. */
constexpr uint32_t kOffEvtMirror   = 0x60u;
constexpr uint32_t kOffChnenblBase = 0x200u;
constexpr uint32_t kChnenblCount   = 48u;

/* Base is 0x83FB0000 per MCIMX51RM Table 2-1 (System Memory Map) and the guest's
   own access; the Ch 52 register-table base 0x83FD4000 is a doc error inherited
   from the i.MX31/i.MX35 SDMA. */
class Imx51Sdma : public FreescaleSdmaBase<0x83FB0000u, SocFamily::iMX51> {
public:
    using FreescaleSdmaBase::FreescaleSdmaBase;

    void OnReady() override {
        FreescaleSdmaBase::OnReady();
        BindUart1();
    }

    /* The UART<->SDMA event binding is host-side wiring, not serialized machine
       state; re-assert it after a hibernation restore (RestoreState does not
       carry it), same as PostRestore re-drives the INTC level. */
    void PostRestore() override {
        FreescaleSdmaBase::PostRestore();
        BindUart1();
    }

protected:
    void AssertIrqLine()   override { emu_.Get<IrqController>().AssertIrq(kTzicSourceSdma); }
    void DeassertIrqLine() override { emu_.Get<IrqController>().DeAssertIrq(kTzicSourceSdma); }

    uint32_t Chnenbl(uint32_t event) const override {
        return event < kChnenblCount ? chnenbl_[event] : 0u;
    }

    /* cspddk runs every i.MX51 SDMA channel with extended 12-byte BDs: its
       per-event channel setup (sub_C09C3FAC) sets the EXTD flag for all events,
       and DDKSdmaGet/SetBufDesc index BDs at 12*idx. */
    uint32_t BdStride(uint32_t) const override { return 12u; }

    bool ReadExtra(uint32_t off, uint32_t& out) override {
        if (off == kOffEvtMirror) { out = 0; return true; }   /* no DMA-request events */
        if (off >= kOffChnenblBase && off < kOffChnenblBase + kChnenblCount * 4
            && (off & 0x3u) == 0) {
            out = chnenbl_[(off - kOffChnenblBase) / 4]; return true;
        }
        return false;
    }
    bool WriteExtra(uint32_t off, uint32_t value) override {
        if (off >= kOffChnenblBase && off < kOffChnenblBase + kChnenblCount * 4
            && (off & 0x3u) == 0) {
            chnenbl_[(off - kOffChnenblBase) / 4] = value; return true;
        }
        return false;   /* EVT_MIRROR is read-only -> write halts */
    }
    void SaveExtra(StateWriter& w) override    { w.WriteBytes(chnenbl_, sizeof(chnenbl_)); }
    void RestoreExtra(StateReader& r) override { r.ReadBytes(chnenbl_, sizeof(chnenbl_)); }
    void ResetExtra() override { for (auto& c : chnenbl_) c = 0; }

private:
    /* UART1 (COM1) moves serial data over SDMA. DMA-request / CHNENBL index for
       UART1 is RX=18, TX=19 (Linux imx51.dtsi `&sdma 18`/`19`; MCIMX51RM Table 3-3
       "Event 17/18" is +1-offset from the CHNENBL index the guest BSP programs). */
    void BindUart1() { emu_.Get<Imx51Uart1>().BindSdma(this, 19u, 18u); }

    uint32_t chnenbl_[kChnenblCount] = {};
};

}  /* namespace */

REGISTER_SERVICE(Imx51Sdma);
