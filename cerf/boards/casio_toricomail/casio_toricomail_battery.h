#pragma once

#include "../../core/service.h"
#include "../../host/battery_widget.h"

class CasioToricomailBattery : public Service {
public:
    explicit CasioToricomailBattery(CerfEmulator& e) : Service(e), battery_(e) {}

    bool ShouldRegister() override;
    void OnReady() override;

    int  FillPercent() const { return battery_.FillPercent(); }
    bool IsOnBattery() const { return battery_.IsOnBattery(); }

private:
    void DrivePowerPins();

    BatteryWidget battery_;
};
