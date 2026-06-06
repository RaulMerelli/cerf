#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* SA-1110 Multimedia Communications Port (Dev Man Table 11-19, base
   0x80060000), an off-chip audio/telecom codec link enabled by MCE in
   MCCR0. Modelled in the reset/disabled state (MCE=0): data and status
   read idle, no transfers. The Jornada 720 OAL keeps it disabled (audio
   is on the SA-1111 + UDA1344) and only writes MCCR0=0. */
class Sa1110Mcp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::SA1110;
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
            case 0x10: return 0;   /* MCDR2. */
            case 0x18: return 0;   /* MCSR — idle, no FIFO service requests. */
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - MmioBase()) {
            case 0x00: mccr0_ = value; return;
            case 0x08: case 0x0C: case 0x10: return;  /* MCDR* — dropped while MCE=0. */
            case 0x18: return;                        /* MCSR W1C status — idle. */
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

private:
    uint32_t mccr0_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(Sa1110Mcp);
