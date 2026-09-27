#include "philips_nino_300_battery.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_widget_registry.h"
#include "../board_context.h"
#include "philips_nino_300_id.h"

bool PhilipsNino300Battery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardId() == BoardId::PhilipsNino300;
}

void PhilipsNino300Battery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
}

REGISTER_SERVICE(PhilipsNino300Battery);
