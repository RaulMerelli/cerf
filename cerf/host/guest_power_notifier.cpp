#include "guest_power_notifier.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "host_window.h"
#include "uart_screen.h"

REGISTER_SERVICE(GuestPowerNotifier);

void GuestPowerNotifier::Banner(const char* line) {
    LOG(Caution, "%s", line);
    auto& uart = emu_.Get<UartScreen>();
    uart.AddLine("");
    uart.AddLine("==================================================");
    uart.AddLine(line);
    uart.AddLine("==================================================");
}

void GuestPowerNotifier::NotifyPowerDown() {
    Banner("   CERF: GUEST POWERED DOWN (deep sleep)");
    emu_.Get<HostWindow>().ShowUartTab(/*rearm=*/false);
}

void GuestPowerNotifier::NotifyReboot() {
    Banner("   CERF: GUEST REBOOT INITIATED");
    emu_.Get<HostWindow>().ShowUartTab(/*rearm=*/true);
}
