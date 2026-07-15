#include "sharp_mobilon_hc4100_battery.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_widget_registry.h"
#include "../board_context.h"

bool SharpMobilonHc4100Battery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
}

void SharpMobilonHc4100Battery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
}

REGISTER_SERVICE(SharpMobilonHc4100Battery);
