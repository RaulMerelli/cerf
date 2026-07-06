#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* VR4102 BCU (Bus Control Unit), Internal I/O Space 2 base 0x0B000000, block
   0x0B000000-0x0B00001F (UM Table 5-10 / Table 10-1). 16-bit registers. */
constexpr uint32_t kBase = 0x0B000000u;
constexpr uint32_t kSize = 0x20u;

constexpr uint32_t kOffCntReg1 = 0x00u;   /* BCUCNTREG1 (UM 10.2.1) */
constexpr uint32_t kOffCntReg2 = 0x02u;   /* BCUCNTREG2 (UM 10.2.2) */

class Vr4102Bcu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    /* CNTREG1/CNTREG2 reset to 0 (UM 10.2.1/10.2.2 RTCRST + Other resets = 0);
       CNTREG1 DRAM64=0 is the MobilePro 700's 16-Mbit/8-MB config. */
    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kOffCntReg1: return cntreg1_;
            case kOffCntReg2: return cntreg2_;
            default: HaltUnsupportedAccess("BCU ReadHalf", addr, 0);
        }
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kOffCntReg1: cntreg1_ = value & 0xF553u; return;  /* reserved D11/9/7/5/3/2 read 0 */
            case kOffCntReg2: cntreg2_ = value & 0x1u; return;  /* only GMODE (D0) is R/W */
            default: HaltUnsupportedAccess("BCU WriteHalf", addr, value);
        }
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("BCU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("BCU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("BCU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("BCU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(cntreg1_); w.Write(cntreg2_); }
    void RestoreState(StateReader& r) override { r.Read(cntreg1_); r.Read(cntreg2_); }

private:
    uint16_t cntreg1_ = 0;
    uint16_t cntreg2_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Bcu);
