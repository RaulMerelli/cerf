#include "guest_power_notifier.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "boot_screen.h"
#include "frame_renderer.h"
#include "host_canvas.h"
#include "host_window.h"
#include "hw_screen.h"

REGISTER_SERVICE(GuestPowerNotifier);

void GuestPowerNotifier::Banner(const char* line) {
    LOG(Caution, "%s\n", line);
    auto& uart = emu_.Get<HwScreen>();
    uart.AddLine("");
    uart.AddLine(line);
}

void GuestPowerNotifier::NotifyPowerDown() {
    Banner("!! CERF: Power down !!");
    emu_.Get<HostWindow>().RunOnUiThread([this] { emu_.Get<HostCanvas>().RememberTabForResume(); });
    emu_.Get<HostWindow>().ShowHwScreenTab(false);
}

void GuestPowerNotifier::NotifyReboot() {
    Banner("!! CERF: Soft reset !!");
    if (auto* fr = emu_.TryGet<FrameRenderer>()) fr->RearmContentLatch();
    emu_.Get<BootScreen>().Restart();
    emu_.Get<HostWindow>().ShowStartupTab(true);
}

void GuestPowerNotifier::NotifyResume() {
    Banner("!! CERF: Resuming !!");
    emu_.Get<HostWindow>().RunOnUiThread([this] { emu_.Get<HostCanvas>().RestoreTabForResume(); });
}

void GuestPowerNotifier::NotifyHardReset() {
    Banner("!! CERF: Hard reset !!");
}
