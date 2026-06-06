#include "jornada720_battery.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_widget_registry.h"
#include "../../socs/sa1110/sa1110_gpio.h"
#include "../board_detector.h"

bool Jornada720Battery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Jornada720;
}

void Jornada720Battery::OnReady() {
    battery_.SetChangeHandler([this] { DriveAcPins(); });
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
    DriveAcPins();   /* publish the initial AC/charging pin state */
}

void Jornada720Battery::DriveAcPins() {
    const bool on_batt = battery_.IsOnBattery();
    auto& gpio = emu_.Get<Sa1110Gpio>();
    gpio.DriveInputPin(4, on_batt);    /* GPIO4 high  -> ACLineStatus offline */
    gpio.DriveInputPin(26, on_batt);   /* GPIO26 high -> not charging          */
}

REGISTER_SERVICE(Jornada720Battery);
