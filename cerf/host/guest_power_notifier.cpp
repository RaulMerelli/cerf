#include "guest_power_notifier.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "frame_renderer.h"
#include "host_window.h"
#include "uart_screen.h"

REGISTER_SERVICE(GuestPowerNotifier);

void GuestPowerNotifier::Banner(const char* line) {
    LOG(Caution, "%s\n", line);
    auto& uart = emu_.Get<UartScreen>();
    uart.AddLine("");
    uart.AddLine("=========================");
    uart.AddLine(line);
    uart.AddLine("=========================");
}

void GuestPowerNotifier::NotifyPowerDown() {
    Banner(" POWER DOWN (deep sleep)");
    emu_.Get<HostWindow>().ShowUartTab(/*rearm=*/false);
}

void GuestPowerNotifier::NotifyReboot() {
    Banner(" DEVICE REBOOT");
    /* Renderer first: while the renderer still reports the stale frame,
       the canvas re-latches off the UART tab on its next tick. */
    if (auto* fr = emu_.TryGet<FrameRenderer>()) fr->RearmContentLatch();
    emu_.Get<HostWindow>().ShowUartTab(/*rearm=*/true);
}

void GuestPowerNotifier::NotifyHardReset() {
    Banner(" HARD RESET (RAM cleared)");
}
