#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* SA-1110 MCP (Dev Man Table 11-19, base 0x80060000): off-chip audio/telecom
   codec link. The codec is not modelled, so an MCDR2 (+0x10) access completes
   instantly — MCSR (+0x18) reports CWC (bit 12) + CRC (bit 13) set (Dev Man
   §11.12.6.13/.14) so the kernel's codec poll (nk.exe sub_80039080) exits. */
class Sa11xxMcp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && (bd->GetSoc() == SocFamily::SA1110 || bd->GetSoc() == SocFamily::SA1100);
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x80060000u; }
    uint32_t MmioSize() const override { return 0x00000060u; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - MmioBase()) {
            case 0x00: return mccr0_;
            case 0x08: return 0;   /* MCDR0 — no codec data while MCE=0. */
            case 0x0C: return 0;   /* MCDR1. */
            case 0x10: return 0;   /* MCDR2 — no codec, read data reads 0. */
            case 0x18: return codec_done_ ? 0x3000u : 0u;  /* MCSR: CWC|CRC set. */
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - MmioBase()) {
            case 0x00: mccr0_ = value; return;
            case 0x08: case 0x0C: return;  /* MCDR0/1 — dropped, no codec. */
            case 0x10: codec_done_ = true; return;  /* MCDR2 codec access completes. */
            case 0x18: return;                       /* MCSR W1C status. */
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

private:
    uint32_t mccr0_ = 0;
    bool     codec_done_ = false;
};

}  /* namespace */

REGISTER_SERVICE(Sa11xxMcp);
