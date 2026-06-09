#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "sa11xx_mcp_codec.h"

namespace {

/* SA-11x0 MCP codec link. MCDR2 (+0x10) command: reg=bits 20:17, bit16=R/W,
   data=15:0; MCSR (+0x18) bit12 CWC / bit13 CRC = write/read done (RE'd from
   820 hplib.dll UcbRegister{Read,Write} @ 0x11A14FC / 0x11A156C). Routes to a
   registered Sa11xxMcpCodec; codec-less boards must read 0, not fault. */
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
            case 0x08: return 0;   /* MCDR0 — no telecom data while MCE=0. */
            case 0x0C: return 0;   /* MCDR1. */
            case 0x10: return mcdr2_read_;   /* MCDR2: last codec read data. */
            case 0x18: return mcsr_;         /* MCSR: CWC|CRC sticky after access. */
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - MmioBase()) {
            case 0x00: mccr0_ = value; return;
            case 0x08: case 0x0C: return;  /* MCDR0/1 — dropped, no telecom. */
            case 0x10: RouteCodecCommand(value); return;
            case 0x18: return;             /* MCSR W1C status. */
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

private:
    void RouteCodecCommand(uint32_t cmd) {
        const uint8_t reg      = static_cast<uint8_t>((cmd >> 17) & 0xFu);
        const bool    is_write = (cmd & 0x10000u) != 0;
        auto* codec = emu_.TryGet<Sa11xxMcpCodec>();
        if (is_write) {
            if (codec) codec->WriteReg(reg, static_cast<uint16_t>(cmd & 0xFFFFu));
        } else {
            mcdr2_read_ = codec ? codec->ReadReg(reg) : 0u;
        }
        mcsr_ |= 0x3000u;   /* synchronous codec: CWC (b12) + CRC (b13) done. */
    }

    uint32_t mccr0_      = 0;
    uint32_t mcsr_       = 0;
    uint16_t mcdr2_read_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(Sa11xxMcp);
