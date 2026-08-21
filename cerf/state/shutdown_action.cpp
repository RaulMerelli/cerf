#include "shutdown_action.h"

#include "../boot/guest_cold_boot.h"
#include "../core/cerf_emulator.h"
#include "../host/host_window.h"
#include "../socs/guest_cpu_reset.h"
#include "hibernation.h"
#include "shutdown_dialog.h"

REGISTER_SERVICE(ShutdownAction);

void ShutdownAction::Perform(ShutdownChoice c) {
    auto& win = emu_.Get<HostWindow>();
    switch (c) {
        case ShutdownChoice::Cancel:
            return;
        case ShutdownChoice::SoftReset:
            emu_.Get<GuestCpuReset>().WarmReset();
            return;
        case ShutdownChoice::HardReset:
            emu_.Get<GuestColdBoot>().RequestHardReset();
            return;
        case ShutdownChoice::ExitSave:
            win.ShowHwScreenTab(false);
            emu_.Get<Hibernation>().SaveAsync(L"", [this] {
                auto& w = emu_.Get<HostWindow>();
                w.RunOnUiThread([this] {
                    emu_.Get<HostWindow>().BeginShutdownTeardown();
                });
            });
            return;
        case ShutdownChoice::Exit:
            win.BeginShutdownTeardown();
            return;
    }
}
