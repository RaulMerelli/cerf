#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../host/guest_deep_sleep.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

namespace {

class S3C2410ClockPower : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::S3C2410;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        /* Power-off+wake resets the chip on real S3C2410; CERF's boot-ROM resume
           skips StartUp, so reset CLKCON here on every reset delivery (incl. the
           deep-sleep wake) — else the OS reads the stale 0x7fff8 power-off magic
           back and re-enters sleep. Reset value 0x7FFF0 = S3C2410A UM p.239. */
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this] { storage_[0x0Cu / 4u] = 0x7FFF0u; });
    }

    uint32_t MmioBase() const override { return 0x4C000000u; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* JIT-thread-only register file (no worker thread) — the JIT is paused
       during save/restore, so no lock is needed. */
    void SaveState(StateWriter& w) override    { w.WriteBytes(storage_, sizeof(storage_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(storage_, sizeof(storage_)); }

private:
    static constexpr size_t kSlotCount = 7;  /* LOCKTIME..CAMDIVN */
    uint32_t storage_[kSlotCount] = {};
};

uint32_t S3C2410ClockPower::ReadWord(uint32_t addr) {
    const uint32_t slot = (addr - MmioBase()) / 4u;
    if (slot >= kSlotCount) {
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }
    const uint32_t value = storage_[slot];
    LOG(SocClkpwr, "read  +0x%02X -> 0x%08X\n",
        addr - MmioBase(), value);
    return value;
}

void S3C2410ClockPower::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t slot = (addr - MmioBase()) / 4u;
    if (slot >= kSlotCount) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    LOG(SocClkpwr, "write +0x%02X = 0x%08X\n",
        addr - MmioBase(), value);
    /* CLKCON (+0x0C) <- 0x7fff8 is the S3C2410 power-off write (cpupoweroff.s
       "Power Off !!"). Enter deep sleep: halt the CPU + run the recovery prompt. */
    if (slot == 0x0Cu / 4u && value == 0x7fff8u) {
        emu_.Get<GuestDeepSleep>().Enter();
    }
    storage_[slot] = value;
}

}  /* namespace */

REGISTER_SERVICE(S3C2410ClockPower);
