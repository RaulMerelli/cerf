#include "guest_power_notifier.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "boot_screen.h"
#include "frame_renderer.h"
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
    emu_.Get<HostWindow>().ShowHwScreenTab(/*rearm=*/false);
}

void GuestPowerNotifier::Relaunch(const char* line, bool resuming) {
    Banner(line);
    /* Renderer first: while the renderer still reports the stale frame, the
       canvas re-latches to the framebuffer off the startup tab on its next tick. */
    if (auto* fr = emu_.TryGet<FrameRenderer>()) fr->RearmContentLatch();
    emu_.Get<BootScreen>().Restart(resuming);
    emu_.Get<HostWindow>().ShowStartupTab(/*rearm=*/true);
}

void GuestPowerNotifier::NotifyReboot() { Relaunch("!! CERF: Soft reset !!", /*resuming=*/false); }

void GuestPowerNotifier::NotifyResume() { Relaunch("!! CERF: Resuming !!",  /*resuming=*/true); }

void GuestPowerNotifier::NotifyHardReset() {
    Banner("!! CERF: Hard reset !!");
}
