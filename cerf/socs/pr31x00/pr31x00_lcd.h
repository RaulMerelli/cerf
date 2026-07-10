#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class Pr31x00Intc;

/* Philips PR31x00 Video Module, TMPR3911/3912 ch.17. Registers $028-$05C. */
class Pr31x00Lcd : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override { StopWorker(); }

    uint32_t MmioBase() const override { return 0x10C00028u; }
    uint32_t MmioSize() const override { return 0x38u; }   /* $028-$05F */

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 LCD ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 LCD ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 LCD WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 LCD WriteHalf", addr, v); }

    /* Consumed by Pr31x00LcdRenderer. */
    bool     IsEnabled() const;
    bool     IsInverted() const;
    uint32_t GetFbPa() const;
    uint32_t GetGuestW() const;
    uint32_t GetGuestH() const;
    uint32_t GetBitsPerPixel() const;

    /* Maps a raw pixel value to its 4-bit gray shade. 4-bit gray needs no LUT;
       2-bit gray selects one of four BLUESEL nibbles (§17.3.7). */
    uint32_t ShadeFor(uint32_t raw) const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    /* envid_ and frame_period_ns_ are derived from reg_, not serialized, and the
       frame worker parks until they are re-published. */
    void PostRestore() override { PublishFrameTiming(); }

private:
    void StorePattern(uint32_t idx, uint32_t addr, uint32_t value,
                      uint32_t recommended, const char* op);

    /* LCDINT fires at the end of each video frame (§8.3.1) and is this board's
       only periodic interrupt, so the CE scheduler stops without it. */
    void PublishFrameTiming();
    void WorkerLoop();
    void StopWorker();

    static constexpr uint32_t kRegs = 14;   /* VIDEO_CTL1..CTL14 */

    uint32_t reg_[kRegs] = {};

    Pr31x00Intc* intc_ = nullptr;

    std::atomic<bool>     envid_{false};
    std::atomic<uint64_t> frame_period_ns_{0};
    std::atomic<bool>     stop_{false};

    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    std::thread             worker_;
};
