#include "serial_modem_card_menu.h"

#include "../../core/cerf_emulator.h"

#include <utility>

REGISTER_SERVICE(SerialModemCardMenu);

std::vector<WidgetMenuItem> SerialModemCardMenu::BuildInsertMenu(
    std::function<void()> on_insert) {
    std::vector<WidgetMenuItem> items;

    auto note = [&items](const wchar_t* text) {
        WidgetMenuItem it;
        it.label   = text;
        it.enabled = false;
        items.push_back(std::move(it));
    };

    WidgetMenuItem insert;
    insert.label    = L"Insert";
    insert.on_click = std::move(on_insert);
    items.push_back(std::move(insert));

    items.push_back({});

    note(L"After inserting, to get online:");
    note(L"   1.  Open the dial-up / network connections app");
    note(L"   2.  Create a new dial-up connection on this modem");
    note(L"   3.  Dial any phone number (for example 555)");
    note(L"   4.  User name and password can be left blank");

    return items;
}
