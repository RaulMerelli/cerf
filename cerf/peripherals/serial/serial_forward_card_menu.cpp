#include "serial_forward_card_menu.h"

#include "host_serial_ports.h"

#include "../../core/cerf_emulator.h"

#include <utility>

REGISTER_SERVICE(SerialForwardCardMenu);

std::vector<WidgetMenuItem> SerialForwardCardMenu::BuildInsertMenu(
    std::function<void(std::wstring host_port)> on_insert) {
    std::vector<WidgetMenuItem> items;

    auto note = [&items](const wchar_t* text) {
        WidgetMenuItem it;
        it.label   = text;
        it.enabled = false;
        items.push_back(std::move(it));
    };

    const std::vector<std::wstring> ports = emu_.Get<HostSerialPorts>().Enumerate();
    if (ports.empty()) {
        note(L"   (no host serial ports found)");
    } else {
        for (const std::wstring& p : ports) {
            WidgetMenuItem it;
            it.label    = p;
            it.on_click = [on_insert, p] { on_insert(p); };
            items.push_back(std::move(it));
        }
    }

    items.push_back({});

    note(L"Bridges the guest serial port to a real host COM port.");
    note(L"Pick a host port above to forward the guest serial port to.");
    note(L"Open the guest COMx with a raw terminal (e.g. CE PuTTY).");
    note(L"Hint: If you want to emulate COM pair on your computer/VM");
    note(L"so you can attach e.g. ActiveSync - learn how to use com0com.");

    return items;
}
