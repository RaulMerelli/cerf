#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

/* NEC VR41xx KIU (Keyboard Interface Unit), 96-key matrix scanner. VR4102 UM ch.21 ==
   VR4121 UM ch.22 (Table 21-1 == Table 22-1; register bits identical). */
class Vr41xxKiu : public Peripheral {
public:
    using Peripheral::Peripheral;

    ~Vr41xxKiu() override { StopWorker(); }
    void OnShutdown() override { StopWorker(); }

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x0B000180u; }
    uint32_t MmioSize() const override { return 0x20u; }

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("KIU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("KIU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("KIU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("KIU WriteWord", addr, v); }

    /* matrix_index = 16*KIUDATn + bit (0..95): keybddr's scan-index encoding
       (MP700 keybddr sub_15B4848 @ 0x15B0978). */
    void SetKeyState(uint8_t matrix_index, bool pressed);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    mutable std::mutex mtx_;

    uint16_t matrix_[6] = {0, 0, 0, 0, 0, 0};   /* KIUDAT0-5: bit set = key pressed. */
    uint16_t scanrep_   = 0x0001;   /* KIUSCANREP reset row: D0 ATSCAN = 1 (VR4102 UM 21.2.2, VR4121 UM 22.2.2). */
    uint16_t causes_    = 0;
    bool     scanning_  = false;
    uint16_t wintvl_    = 0;   /* KIUWKI WINTVL(9:0): inter-scan interval in 30us units (VR4102 UM 21.2.5, VR4121 UM 22.2.5); reset 0 = No Wait. */

    bool EnabledLocked() const;
    bool AnyKeyDownLocked() const;
    void PublishCausesLocked();
    void ApplyResetLocked();

    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    std::thread             worker_;
    std::atomic<bool>       stop_{false};

    void NotifyWorker();
    void StopWorker();
    void WorkerLoop();
};
