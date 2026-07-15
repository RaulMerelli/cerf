#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* NEC VR4102 LED Control Unit, 0x0B000240 block (UM ch.23): a programmable
   status-LED blinker. CERF has no chassis-LED surface, so the config registers
   are R/W latches with no visual effect; leddrv.dll has no IST, so LEDINTREG
   (auto-stop interrupt) has no consumer and stays born-FATAL. */
constexpr uint32_t kBase = 0x0B000240u;   /* UM Table 23-1, p453. */

constexpr uint32_t kOffHts   = 0x00u;   /* LEDHTSREG 0x240 (+ LEDLTSREG 0x242, one 32-bit store) */
constexpr uint32_t kOffResvA = 0x04u;   /* reserved 0x244 (leddrv init zeroes it) */
constexpr uint32_t kOffResvB = 0x06u;   /* reserved 0x246 */
constexpr uint32_t kOffCnt   = 0x08u;   /* LEDCNTREG 0x248 */
constexpr uint32_t kOffAstc  = 0x0Au;   /* LEDASTCREG 0x24A */

constexpr uint16_t kCntMask  = 0x0003u; /* LEDSTOP(D1) | LEDENABLE(D0), UM 23.2.3 p456 */

/* LEDCNTREG RTCRST row: LEDSTOP(D1) = 1, LEDENABLE(D0) = 0; its Other-resets row is
   "Previous value is retained" (UM 23.2.3, p456). */
constexpr uint16_t kCntPowerOn = 0x0002u;

class Vr4102Led : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) cnt_ = kCntPowerOn;
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x10u; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kOffCnt) return cnt_;   /* leddrv RMWs LEDCNTREG */
        HaltUnsupportedAccess("LED ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kOffCnt: cnt_ = value & kCntMask; return;
            /* Auto-stop count + the reserved gap the driver zeroes: no CERF effect. */
            case kOffAstc: case kOffResvA: case kOffResvB: return;
            default: HaltUnsupportedAccess("LED WriteHalf", addr, value);
        }
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - kBase == kOffHts) return;   /* LEDHTSREG+LEDLTSREG blink timing */
        HaltUnsupportedAccess("LED WriteWord", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("LED ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("LED ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("LED WriteByte", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(cnt_); }
    void RestoreState(StateReader& r) override { r.Read(cnt_); }

private:
    uint16_t cnt_ = kCntPowerOn;   /* LEDCNTREG */
};

}  // namespace

REGISTER_SERVICE(Vr4102Led);
