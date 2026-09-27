#include "simpad_sl4_battery.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_widget_registry.h"
#include "../board_context.h"
#include "simpad_sl4_id.h"

bool SimpadSl4Battery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardId() == BoardId::SimpadSl4;
}

void SimpadSl4Battery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
}

REGISTER_SERVICE(SimpadSl4Battery);
