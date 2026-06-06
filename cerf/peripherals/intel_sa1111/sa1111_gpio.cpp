#include "../peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../peripheral_dispatcher.h"

namespace {

/* SA-1111 GPIO blocks A/B/C (Developer's Manual Table 10-7, base 0x40001000),
   8-bit ports DDR/DWR·DRR/SDR/SSR at stride 0x10. Px_DDR (§10.3.3) 1=input,
   0=output, reset 0xFF (all input). Px_DRR (§10.3.2) reads the external pin
   state — an output pin reads back its DWR latch; nothing drives the SA-1111
   GPIO inputs in CERF yet, so input pins read 0. */
class Sa1111Gpio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x40001000u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off >= 0x30u || (off & 3u)) HaltUnsupportedAccess("ReadWord", addr, 0);
        const uint32_t port = off >> 4, reg = (off >> 2) & 3u;
        switch (reg) {
            case 0: return ddr_[port];
            case 1: return dwr_[port] & ~ddr_[port] & 0xFFu;  /* DRR: output pins read their latch. */
            case 2: return sdr_[port];
            case 3: return ssr_[port];
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off >= 0x30u || (off & 3u)) HaltUnsupportedAccess("WriteWord", addr, value);
        const uint32_t port = off >> 4, reg = (off >> 2) & 3u, v = value & 0xFFu;
        switch (reg) {
            case 0: ddr_[port] = v; return;
            case 1: dwr_[port] = v; return;
            case 2: sdr_[port] = v; return;
            case 3: ssr_[port] = v; return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

private:
    uint32_t ddr_[3] = { 0xFFu, 0xFFu, 0xFFu };  /* reset: all input (§10.3.3). */
    uint32_t dwr_[3] = {};
    uint32_t sdr_[3] = {};
    uint32_t ssr_[3] = {};
};

}  /* namespace */

REGISTER_SERVICE(Sa1111Gpio);
