#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../guest_cpu_reset.h"
#include "imx6_mmio_lane.h"

#include <algorithm>
#include <atomic>
#include <iterator>

namespace {

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
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x08u) {
            return srsr_.load(std::memory_order_acquire);
        }
        if (IsReadableRegister(off)) {
            return regs_[off >> 2];
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x00u) {
            regs_[0] = value & ~0x0000E00Eu;
            return;
        }
        if (off == 0x08u) {
            srsr_.fetch_and(~value, std::memory_order_acq_rel);
            return;
        }
        if (IsWritableRegister(off)) {
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
    static bool IsReadableRegister(uint32_t off) {
        return off == 0x00u || off == 0x04u || off == 0x14u || off == 0x18u ||
               off == 0x1Cu || (off >= 0x20u && off <= 0x44u && (off & 3u) == 0u);
    }

    static bool IsWritableRegister(uint32_t off) {
        return off == 0x18u || (off >= 0x20u && off <= 0x44u && (off & 3u) == 0u);
    }

    void ResetRegisters() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        /* IMX6SDLRM Rev.4 §60.7. */
        regs_[0x00u >> 2] = 0x00000521u;
        regs_[0x18u >> 2] = 0x0000001Fu;
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == 0x08u) {
            srsr_.fetch_and(~lane.value, std::memory_order_acq_rel);
            return;
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    static constexpr uint32_t kSrsrPor = 1u << 0u;
    static constexpr uint32_t kSrsrWatchdog = 1u << 4u;
    static constexpr uint32_t kSrsrWarmBoot = 1u << 16u;

    uint32_t regs_[0x48u / 4u]{};
    std::atomic<uint32_t> srsr_{kSrsrPor};
};

}

REGISTER_SERVICE(Imx6Src);
