#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../boards/board_detector.h"

#include <chrono>
#include <cstdint>
#include <mutex>

namespace {

constexpr uint32_t kSynctimerBasePa = 0x48320000u;
constexpr uint32_t kSynctimerSize   = 0x00001000u;
constexpr uint64_t kClockHz         = 32768ull;

constexpr uint32_t kOffRev       = 0x00;
constexpr uint32_t kOffSysconfig = 0x04;
constexpr uint32_t kOffCr        = 0x10;

class Omap3530Synctimer : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetSoc() == SocFamily::OMAP3530;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        start_time_ = Clock::now();
    }

    uint32_t MmioBase() const override { return kSynctimerBasePa; }
    uint32_t MmioSize() const override { return kSynctimerSize; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

private:
    using Clock = std::chrono::steady_clock;

    uint32_t ComputeCounter() const;

    mutable std::mutex state_mutex_;
    Clock::time_point  start_time_{};
    uint32_t           sysconfig_ = 0;
};

uint32_t Omap3530Synctimer::ComputeCounter() const {
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start_time_).count();
    if (elapsed_ns <= 0) return 0u;
    const uint64_t ticks =
        static_cast<uint64_t>(elapsed_ns) * kClockHz / 1'000'000'000ull;
    return static_cast<uint32_t>(ticks & 0xFFFFFFFFull);
}

uint32_t Omap3530Synctimer::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (off) {
    case kOffRev:       return 0u;
    case kOffSysconfig: return sysconfig_;
    case kOffCr:        return ComputeCounter();
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Omap3530Synctimer::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (off) {
    case kOffRev:       return;              /* read-only */
    case kOffSysconfig: sysconfig_ = value;  return;
    case kOffCr:        return;              /* counter is read-only */
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

}  /* namespace */

REGISTER_SERVICE(Omap3530Synctimer);
