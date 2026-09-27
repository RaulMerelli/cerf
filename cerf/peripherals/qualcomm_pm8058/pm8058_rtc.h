#pragma once

#include "../../core/service.h"

#include <cstdint>
#include <mutex>

class StateWriter;
class StateReader;

class Pm8058Rtc : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* Linux drivers/rtc/rtc-pm8xxx.c pm8058_regs: ctrl 0x1e8, write 0x1ea,
       read 0x1ee, alarm_ctrl 0x1e8, alarm_ctrl2 0x1e9, alarm_rw 0x1f2, with
       NUM_8_BIT_RTC_REGS 4 bytes in each of the three register groups. */
    static constexpr uint16_t kRegCtrl      = 0x01E8u;
    static constexpr uint16_t kRegAlarmCtl2 = 0x01E9u;
    static constexpr uint16_t kRegWrite     = 0x01EAu;
    static constexpr uint16_t kRegRead      = 0x01EEu;
    static constexpr uint16_t kRegAlarmRw   = 0x01F2u;
    static constexpr uint32_t kBytes        = 4u;

    static bool Owns(uint16_t reg) {
        return reg >= kRegCtrl && reg < kRegAlarmRw + kBytes;
    }

    uint8_t ReadReg(uint16_t reg);
    void    WriteReg(uint16_t reg, uint8_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint32_t CounterLocked() const;
    void     LatchLocked();

    mutable std::mutex mtx_;

    uint32_t base_      = 0;
    uint64_t anchor_us_ = 0;
    bool     running_   = false;
    uint8_t  ctrl_       = 0;
    uint8_t  alarm_ctl2_ = 0;
    uint8_t  load_[kBytes]  = {};
    uint8_t  alarm_[kBytes] = {};
};
