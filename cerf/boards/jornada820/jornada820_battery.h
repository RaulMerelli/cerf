#pragma once

#include "../../core/service.h"
#include "../../host/battery_widget.h"

#include <condition_variable>
#include <mutex>
#include <thread>

/* HP Jornada 820 main-battery source: streams the HP MCU's 14-byte status
   packet over SA-1100 SP1 (Sa11xxSp1Uart RX) so hplib.dll's own RX thread
   fills the gwes power gauge. CERF emulates the chip; it must never write the
   gauge DRAM itself. Packet layout + guest decode chain in BuildPacket. */
class Jornada820Battery : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    /* JIT-thread read by the companion ASIC AC-line register (PA 0x18300400
       bit5, gwes sub_8F770): true = on battery (bit5 set), false = on AC. */
    bool IsOnBattery() const { return battery_.IsOnBattery(); }

private:
    void StreamLoop();

    BatteryWidget           battery_;
    std::thread             thread_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    bool                    stop_  = false;
    bool                    dirty_ = false;
};
