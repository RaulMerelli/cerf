#pragma once

#include "../../socs/spi_slave.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

class DevEmuKeyboardController : public SpiSlave {
public:
    using SpiSlave::SpiSlave;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void QueueScancode(uint8_t scancode);

    uint8_t Exchange(uint8_t mosi) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void     ResetDevice();
    void     DriveLineLocked(bool level);
    void     ArmPresentationLocked();
    void     AcceptHostByteLocked(uint8_t mosi);
    void     DeadlineLoop();
    uint32_t GuestCycles() const;

    std::mutex               mutex_;
    std::condition_variable  cv_;
    std::deque<uint8_t>      queue_;
    std::thread              worker_;
    uint32_t                 deadline_    = 0;
    uint32_t                 cmd_index_   = 0;
    uint32_t                 cmd_pos_     = 0;
    bool                     armed_       = false;
    bool                     line_active_ = false;
    bool                     line_seeded_ = false;
    bool                     stop_        = false;
};
