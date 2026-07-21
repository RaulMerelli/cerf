#include "no_emulation_runtime_service.h"

#include "board_not_found_service.h"
#include "cerf_emulator.h"
#include "device_config.h"
#include "device_not_found_service.h"
#include "log.h"

#include "../host/about_dialog.h"
#include "../host/host_dark_mode.h"

REGISTER_SERVICE(NoEmulationRuntimeService);

void NoEmulationRuntimeService::OnReady() {
    EnsureEmulationPrevented();
}

void NoEmulationRuntimeService::EnsureEmulationPrevented() {
    if (checked_) return;
    checked_ = true;

    if (emu_.Get<DeviceConfig>().show_about_instead_of_run) {
        emu_.Get<HostDarkMode>().Init();
        emu_.Get<AboutDialog>().ShowStandalone();
        CerfFatalExit(CERF_FATAL_NORMAL_EXIT);
    }

    if (auto* d = emu_.TryGet<DeviceNotFoundService>()) d->EnsureFound();
    if (auto* b = emu_.TryGet<BoardNotFoundService>())  b->EnsureFound();
}
