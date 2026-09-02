#include "../../core/cerf_emulator.h"
#include "../../socs/freescale_sdma_impl.h"
#include "../../socs/imx6/imx6_gic.h"
#include "../../state/state_stream.h"

namespace {

class Imx6Sdma : public cerf_freescale_sdma_detail::FreescaleSdmaBase<0x020EC000u, SocFamily::iMX6> {
public:
    using FreescaleSdmaBase::FreescaleSdmaBase;

protected:
    /* SDMA AP interrupt = GIC SPI 2. Linux arch/arm/boot/dts/nxp/imx/imx6sl.dtsi
       sdma node: interrupts = <GIC_SPI 2 IRQ_TYPE_LEVEL_HIGH>. */
    void AssertIrqLine() override { emu_.Get<Imx6Gic>().AssertSpi(2); }
    void DeassertIrqLine() override { emu_.Get<Imx6Gic>().DeAssertSpi(2); }

    uint32_t ChnenblBase() const override { return 0x200u; }
    uint32_t ChnenblCount() const override { return kChnenblCount; }

    /* i.MX6-specific divergent registers: EVT_MIRROR @0x54 (RO), CHNENBL0
       at 0x200 with 48 event-enable words (Linux imx-sdma.c sdma_imx6q). */
    bool ReadExtra(uint32_t off, uint32_t& out) override {
        if (off == 0x054u) {
            out = 0;
            return true;
        }
        if (off >= 0x200u && off < 0x200u + kChnenblCount * 4u && (off & 3u) == 0) {
            out = chnenbl_[(off - 0x200u) / 4u];
            return true;
        }
        return false;
    }
    bool WriteExtra(uint32_t off, uint32_t value) override {
        if (off >= 0x200u && off < 0x200u + kChnenblCount * 4u && (off & 3u) == 0) {
            chnenbl_[(off - 0x200u) / 4u] = value;
            return true;
        }
        return false;
    }
    void SaveExtra(StateWriter& w) override { w.WriteBytes(chnenbl_, sizeof(chnenbl_)); }
    void RestoreExtra(StateReader& r) override { r.ReadBytes(chnenbl_, sizeof(chnenbl_)); }
    void ResetExtra() override {
        for (auto& c : chnenbl_)
            c = 0u;
    }

private:
    static constexpr uint32_t kChnenblCount = 48u;
    uint32_t chnenbl_[kChnenblCount] = {};
};

REGISTER_SERVICE(Imx6Sdma);

} // namespace
