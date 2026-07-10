#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x10C000A0u;

/* IR Control 1 (§10.5.1): CARDET<24>(R) BAUDVAL[7:0]<23:16> TESTIR<4> DTINVERT<3>
   RXPWR<2> ENSTATE<1> ENCONSM<0>; bits 31-25 and 15-5 reserved. */
constexpr uint32_t kCtl1Reserved = 0xFE00FFE0u;

/* TESTIR "should not be set" (§10.5.1); ENSTATE starts the carrier detect state
   machine and ENCONSM the Consumer IR function, both of which clock the Period
   Width and OnTime Width counters against a transceiver. */
constexpr uint32_t kCtl1Unmodeled = 0x00000013u;

/* BAUDVAL pre-scales IRCLK, DTINVERT inverts IROUT and RXPWR drives the RXPWR pin;
   none of them moves a frame while the state machines are off. */
constexpr uint32_t kCtl1Writable = 0x00FF000Cu;

class Pr31x00Ir : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::PR31500 || soc == SocFamily::PR31700;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4u; }

    /* CARDET is driven into the SoC by the external analog circuit of Figure 10.3.1,
       which no remote IR transmitter illuminates. */
    uint32_t ReadWord(uint32_t addr) override {
        if (addr == kBase) return ctl1_;
        HaltUnsupportedAccess("PR31x00 IR_CTL1 ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr != kBase) {
            HaltUnsupportedAccess("PR31x00 IR_CTL1 WriteWord", addr, value);
        }
        if (value & kCtl1Reserved) {
            HaltUnsupportedAccess("PR31x00 IR_CTL1 reserved", addr, value);
        }
        if (value & kCtl1Unmodeled) {
            HaltUnsupportedAccess("PR31x00 IR_CTL1 arms the IR state machines", addr, value);
        }
        ctl1_ = value & kCtl1Writable;   /* CARDET is read-only */
    }

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 IR ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 IR ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 IR WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 IR WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(ctl1_); }
    void RestoreState(StateReader& r) override { r.Read(ctl1_); }

private:
    uint32_t ctl1_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(Pr31x00Ir);
