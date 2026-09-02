#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../guest_cpu_reset.h"

#include <algorithm>
#include <atomic>
#include <iterator>

namespace {

/* SCR warm-reset and per-core bits self-clear in HW; clearing on write is mandatory or OAL reset-complete poll stalls.
 */
class Imx6Src : public Peripheral, public ResetCauseLatch {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        ResetRegisters();
        srsr_.store(kSrsrPor, std::memory_order_release);
        emu_.Get<PeripheralDispatcher>().Register(this);
        auto& reset = emu_.Get<GuestCpuReset>();
        reset.SetCauseLatch(this);
        reset.RegisterResetListener([this](ResetLineKind) { ResetRegisters(); });
    }

    /* IMX6DQRM Rev.2 section 60.7.3: SRSR bit 0 is ipp_reset_b
       (power-on), bit 4 is wdog_rst_b and bit 16 is warm_boot. */
    void LatchColdReset() override {
        srsr_.store(kSrsrPor, std::memory_order_release);
    }
    void LatchWarmReset() override {
        srsr_.store(kSrsrWarmBoot, std::memory_order_release);
    }
    void LatchWatchdogReset() override {
        srsr_.store(kSrsrWatchdog, std::memory_order_release);
    }

    uint32_t MmioBase() const override { return 0x020D8000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x08u) return srsr_.load(std::memory_order_acquire);
        if (off <= 0x44u && (off & 3u) == 0) return regs_[off >> 2];
        HaltUnsupportedAccess("read32", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x00u) {
            /* SCR: clear the one-shot core/warm-reset request bits so the
               OAL's "reset issued" poll sees them self-clear (RM section 60.7.1:
               *_RST and warm_reset_enable are W1S, hardware-cleared). */
            regs_[0] = value & ~0x0000E00Eu;
            return;
        }
        if (off == 0x08u) { /* SRSR: write-1-to-clear. */
            srsr_.fetch_and(~value, std::memory_order_acq_rel);
            return;
        }
        if (off <= 0x44u && (off & 3u) == 0) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        uint32_t snapshot[0x48u / 4u]{};
        std::copy(std::begin(regs_), std::end(regs_), std::begin(snapshot));
        snapshot[0x08u >> 2] = srsr_.load(std::memory_order_acquire);
        w.WriteBytes(snapshot, sizeof(snapshot));
    }

    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_, sizeof(regs_));
        srsr_.store(regs_[0x08u >> 2], std::memory_order_release);
        regs_[0x08u >> 2] = 0u;
    }

private:
    void ResetRegisters() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        regs_[0x00u >> 2] = 0x00000521u; /* SCR reset, IMX6DQRM Table 60-3. */
        regs_[0x18u >> 2] = 0x0000001Fu; /* SIMR: all reset sources masked. */
    }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    static constexpr uint32_t kSrsrPor = 1u << 0u;
    static constexpr uint32_t kSrsrWatchdog = 1u << 4u;
    static constexpr uint32_t kSrsrWarmBoot = 1u << 16u;

    uint32_t regs_[0x48u / 4u]{};
    std::atomic<uint32_t> srsr_{kSrsrPor};
};

} /* namespace */

REGISTER_SERVICE(Imx6Src);
