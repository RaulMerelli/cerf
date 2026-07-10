#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/guest_power_notifier.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* PR31x00 Power module. The single Power Control Register sits at offset $1C4 of
   the Internal Function Registers (PA 0x10C00000, TMPR3911/3912 Table 4.2.1);
   field layout and reset values per §12.3.1. */
constexpr uint32_t kBase = 0x10C001C4u;
constexpr uint32_t kSize = 0x4u;

constexpr uint32_t kOnButn    = 1u << 31;   /* R: ONBUTN signal status */
constexpr uint32_t kPwrInt    = 1u << 30;   /* R: PWRINT signal status */
constexpr uint32_t kPwrOk     = 1u << 29;   /* R: PWROK signal status */
constexpr uint32_t kColdStart = 1u << 2;    /* set by RESET: a power-on reset occurred */
constexpr uint32_t kPwrCs     = 1u << 1;
constexpr uint32_t kVccOn     = 1u << 0;

constexpr uint32_t kWritable = 0x1E00FFBFu;   /* VIDRF..DIVMOD, STPTIMERVAL..SELC2MS, BPDBVCC3..VCCON */
constexpr uint32_t kReadOnly = kOnButn | kPwrInt | kPwrOk;

class Pr31x00Power : public Peripheral {
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
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr != kBase) {
            HaltUnsupportedAccess("PR31x00 Power ReadWord", addr, 0);
        }
        return ctl_ | signals_;
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr != kBase) {
            HaltUnsupportedAccess("PR31x00 Power WriteWord", addr, value);
        }
        const uint32_t prev = ctl_;
        ctl_ = value & kWritable;

        /* "When powering down the system, [VCCON] must be cleared simultaneously
           with the PWRCS bit" (§12.3.1) - the guest turning the machine off. */
        const bool was_on = (prev & (kVccOn | kPwrCs)) != 0u;
        const bool now_off = (ctl_ & (kVccOn | kPwrCs)) == 0u;
        if (was_on && now_off) {
            emu_.Get<GuestPowerNotifier>().NotifyPowerDown();
        }
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("PR31x00 Power ReadByte", addr, 0); }
    uint16_t ReadHalf (uint32_t addr) override { HaltUnsupportedAccess("PR31x00 Power ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 Power WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 Power WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(ctl_); w.Write(signals_); }
    void RestoreState(StateReader& r) override { r.Read(ctl_); r.Read(signals_); }

private:
    uint32_t ctl_ = kColdStart;

    uint32_t signals_ = kPwrOk;

    static_assert((kWritable & kReadOnly) == 0u, "writable and read-only fields overlap");
};

}  /* namespace */

REGISTER_SERVICE(Pr31x00Power);
