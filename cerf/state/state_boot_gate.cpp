#include "state_boot_gate.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../jit/jit_runner.h"
#include "hibernation.h"

REGISTER_SERVICE(StateBootGate);

void StateBootGate::OnReady() {
    emu_.Get<JitRunner>().RegisterPreStartHook([this] { Run(); });
}

void StateBootGate::Run() {
    auto& hib = emu_.Get<Hibernation>();
    if (!hib.DefaultStateExists()) return;   /* nothing to resume -> cold boot */

    /* Restore drives its own UART progress and, on failure, holds the screen
       for a keypress before falling through to a cold boot. */
    switch (emu_.Get<DeviceConfig>().boot_mode) {
        case StateBootMode::Cold:   break;                                /* ignore state.img */
        case StateBootMode::Warm:
            hib.Restore(L"", /*ram_only=*/true,  /*cold_boot_on_failure=*/true);  break;
        case StateBootMode::Resume:
            hib.Restore(L"", /*ram_only=*/false, /*cold_boot_on_failure=*/true);  break;
    }
}
