#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

namespace {

/* i.MX51 Keypad Port (KPP), MCIMX51RM Ch.43, base 0x73F94000 (Table 2-1).
   16-bit R/W KPCR/KPSR/KDDR/KPDR at 0x0/0x2/0x4/0x6, reset KPSR=0x0400 (Table
   43-3). */
constexpr uint32_t kBase = 0x73F94000u;
constexpr uint32_t kSize = 0x00004000u;   /* AIPS-1 16 KB peripheral slot */

constexpr uint32_t kKpsrOff   = 0x2u;     /* Table 43-3 */
constexpr uint32_t kKddrOff   = 0x4u;     /* Table 43-3: 1 = output, 0 = input */
constexpr uint32_t kKpdrOff   = 0x6u;     /* Table 43-3 */
constexpr uint16_t kKpsrReset = 0x0400u;  /* Table 43-3 KPSR reset value */

class Imx51Kpp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::iMX51;
    }
    void OnReady() override {
        regs_[kKpsrOff >> 1] = kKpsrReset;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t idx = (addr - kBase) >> 1;
        /* KPDR read = pin state (RM 43.4.3.4): output bits (KDDR=1) read the
           driven latch, input bits read the external level = 0 (no keypad
           wired, active-high so no key = 0). */
        if (idx == (kKpdrOff >> 1)) return regs_[idx] & regs_[kKddrOff >> 1];
        return regs_[idx];
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        regs_[(addr - kBase) >> 1] = value;
    }

    /* JIT-thread-only register file (no worker thread). */
    void SaveState(StateWriter& w) override    { w.WriteBytes(regs_.data(), sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_.data(), sizeof(regs_)); }

private:
    std::array<uint16_t, kSize / 2> regs_{};
};

}  /* namespace */

REGISTER_SERVICE(Imx51Kpp);
