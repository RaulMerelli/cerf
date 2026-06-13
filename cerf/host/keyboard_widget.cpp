#include "keyboard_widget.h"

#include "../core/cerf_emulator.h"
#include "host_icon_cache.h"
#include "host_widget_registry.h"
#include "keyboard_hotkey_menu.h"
#include "keyboard_map.h"
#include "keyboard_mapping_dialog.h"

REGISTER_SERVICE(KeyboardWidget);

bool KeyboardWidget::ShouldRegister() {
    return emu_.TryGet<KeyboardMap>() != nullptr;
}

void KeyboardWidget::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void KeyboardWidget::DrawIcon(HDC dc, const RECT& box) const {
    emu_.Get<HostIconCache>().DrawCentered(dc, box, L"ICON_KEYBOARD");
}

std::vector<WidgetMenuItem> KeyboardWidget::BuildMenu() {
    std::vector<WidgetMenuItem> items;

    WidgetMenuItem see;
    see.label    = L"See keyboard mapping";
    see.on_click = [this] { emu_.Get<KeyboardMappingDialog>().Show(); };
    items.push_back(std::move(see));

    if (auto* hk = emu_.TryGet<KeyboardHotkeyMenu>()) {
        for (auto& sec : hk->HotkeySections()) {
            if (sec.empty()) continue;
            items.push_back(WidgetMenuItem{});   /* separator */
            for (auto& it : sec) items.push_back(std::move(it));
        }
    }
    return items;
}
