#include "serial_modem_card_menu.h"

#include "serial_pccard.h"
#include "../../core/cerf_emulator.h"

#include <memory>

REGISTER_SERVICE(SerialModemCardMenu);

std::vector<WidgetMenuItem> SerialModemCardMenu::BuildInsertMenu(
    PcmciaCardCatalog::CardInserter inserter) {
    std::vector<WidgetMenuItem> items;

    WidgetMenuItem insert;
    insert.label    = L"Insert";
    insert.on_click = [this, inserter] {
        inserter(std::make_unique<SerialPcCard>(emu_));
    };
    items.push_back(std::move(insert));

    items.push_back({});   /* separator */

    auto note = [&items](const wchar_t* text) {
        WidgetMenuItem it;
        it.label   = text;
        it.enabled = false;   /* shown grayed: inline guidance, not clickable */
        items.push_back(std::move(it));
    };
    note(L"After inserting, to get online:");
    note(L"   1.  Open the dial-up / network connections app");
    note(L"   2.  Create a new dial-up connection on this modem");
    note(L"   3.  Dial any phone number (for example 555)");
    note(L"   4.  User name and password can be left blank");

    return items;
}
