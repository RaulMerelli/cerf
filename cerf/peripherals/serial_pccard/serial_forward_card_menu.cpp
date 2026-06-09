#include "serial_forward_card_menu.h"

#include "serial_pccard.h"
#include "../../core/cerf_emulator.h"

#define NOMINMAX
#include <windows.h>

#include <memory>

REGISTER_SERVICE(SerialForwardCardMenu);

std::vector<WidgetMenuItem> SerialForwardCardMenu::BuildInsertMenu(
    PcmciaCardCatalog::CardInserter inserter) {
    std::vector<WidgetMenuItem> items;

    auto note = [&items](const wchar_t* text) {
        WidgetMenuItem it;
        it.label   = text;
        it.enabled = false;   /* shown grayed: inline guidance, not clickable */
        items.push_back(std::move(it));
    };
    const std::vector<std::wstring> ports = EnumerateHostComPorts();
    if (ports.empty()) {
        note(L"   (no host serial ports found)");
    } else {
        for (const std::wstring& p : ports) {
            WidgetMenuItem it;
            it.label    = p;
            it.on_click = [this, inserter, p] {
                inserter(std::make_unique<SerialPcCard>(emu_, p));
            };
            items.push_back(std::move(it));
        }
    }

    items.push_back({});   /* separator */

    note(L"Bridges the guest serial port to a real host COM port.");
    note(L"Pick a host port above to forward the guest serial port to.");
    note(L"Open the guest COMx with a raw terminal (e.g. CE PuTTY).");
    return items;
}

std::vector<std::wstring> SerialForwardCardMenu::EnumerateHostComPorts() const {
    std::vector<std::wstring> ports;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return ports;   /* no serial ports registered on this host */
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        wchar_t data[256];
        DWORD nlen = 256, dlen = sizeof(data), type = 0;
        const LONG rc = RegEnumValueW(key, i, name, &nlen, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(data), &dlen);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc == ERROR_SUCCESS && type == REG_SZ) {
            data[(sizeof(data) / sizeof(wchar_t)) - 1] = L'\0';   /* guarantee NUL */
            ports.emplace_back(data);
        }
    }
    RegCloseKey(key);
    return ports;
}
