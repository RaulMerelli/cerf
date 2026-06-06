#include "../peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../peripheral_dispatcher.h"

namespace {

/* SA-1111 PCMCIA interface (Developer's Manual Table 12-6, base 0x40001800):
   PCCR +0x00 R/W control, PCSSR +0x04 R/W sleep state, PCSR +0x08 read-only
   status. The Jornada boots with no card in either socket, so PCSR's
   card-detect bits 2,3 read 1 (§12.6.1: "0 = card detect valid" — set means
   no card); the control/sleep registers store with no PCMCIA-bus effect. */
class Sa1111Pcmcia : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x40001800u; }
    uint32_t MmioSize() const override { return 0x00000010u; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - MmioBase()) {
            case 0x00: return pccr_;
            case 0x04: return pcssr_;
            case 0x08: return 0x0Cu;   /* PCSR bits 2,3 = no card, both sockets. */
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - MmioBase()) {
            case 0x00: pccr_  = value; return;
            case 0x04: pcssr_ = value; return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

private:
    uint32_t pccr_ = 0, pcssr_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(Sa1111Pcmcia);
