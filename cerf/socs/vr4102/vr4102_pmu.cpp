#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* VR4102 PMU (Power Management Unit), Internal I/O Space 2, 0x0B0000A0-0x0B0000BF
   (UM Table 15-4): PMUINTREG@0x00, PMUCNTREG@0x02, PMUINT2REG@0x04,
   PMUCNT2REG@0x06, 16-bit. */
constexpr uint32_t kBase      = 0x0B0000A0u;
constexpr uint32_t kSize      = 0x20u;
constexpr uint32_t kOffIntReg = 0x00u;   /* PMUINTREG  (UM 15.2.1, p328-329) */
constexpr uint32_t kOffCntReg = 0x02u;   /* PMUCNTREG  (UM 15.2.2, p330-331) */

/* PMUINTREG bit classes (UM p328-329). Cause bits are write-1-to-clear; the two
   lock bits are plain software R/W (never set by hardware); D10 DCDST is a
   read-only pin state and D11 is reserved (both stay 0 - no source drives them). */
constexpr uint16_t kIntW1c    = 0xF33Fu;  /* D15-12 GPIOxINTR, D9 RTCINTR, D8 BATTINH, D5-0 *RST/*INTR */
constexpr uint16_t kIntSwRw   = 0x00C0u;  /* D7 BATTLOCK, D6 CARDLOCK */
constexpr uint16_t kIntRtcRst = 0x0010u;  /* D4 RTCRST */

/* PMUCNTREG bit classes (UM p330-331): D15-8 GPIO3-0 MSK/TRG + D7 STANDBY + D2
   HALTIMERRST are R/W; D6-3/D0 reserved read 0, D1 reserved reads 1. */
constexpr uint16_t kCntWritable = 0xFF84u;
constexpr uint16_t kCntD1Read   = 0x0002u;
constexpr uint16_t kCntReset    = 0x8802u;  /* RTCRST: GPIO3MSK(D15) + GPIO3TRG(D11) + D1 */

class Vr4102Pmu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kOffIntReg: return intreg_;
            case kOffCntReg: return cntreg_;
            default: HaltUnsupportedAccess("PMU ReadHalf", addr, 0);
        }
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kOffIntReg:
                intreg_ &= ~(value & kIntW1c);                          /* ack (clear) written cause bits */
                intreg_ = (intreg_ & ~kIntSwRw) | (value & kIntSwRw);   /* software lock bits: plain R/W */
                return;
            /* HALTIMERRST (D2) is stored R/W but its HAL-timer auto-shutdown
               watchdog is NOT modeled: the guest sets it once in board-init and
               no periodic reset is visible, so arming a ~4 s shutdown timer would
               spuriously power the guest off mid-boot. */
            case kOffCntReg:
                cntreg_ = (value & kCntWritable) | kCntD1Read;
                return;
            default: HaltUnsupportedAccess("PMU WriteHalf", addr, value);
        }
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("PMU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("PMU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PMU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("PMU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(intreg_); w.Write(cntreg_); }
    void RestoreState(StateReader& r) override { r.Read(intreg_); r.Read(cntreg_); }

private:
    /* Cold power-on is the RTC-domain reset, so PMUINTREG powers up with RTCRST
       (D4) set (UM Table 15-1 + p328 reset column); guest nk.exe start()
       (0x9F001CA4: read PMUINTREG, isolate D4, branch) requires D4 set to take
       the cold-boot path. */
    uint16_t intreg_ = kIntRtcRst;
    uint16_t cntreg_ = kCntReset;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Pmu);
