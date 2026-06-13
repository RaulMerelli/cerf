#include "jornada_keys_widget.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_icon_cache.h"
#include "../../host/host_widget_registry.h"

void JornadaKeysWidget::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

WidgetMenuItem JornadaKeysWidget::MakeKeyItem(const wchar_t* label, uint8_t vk) {
    WidgetMenuItem it;
    it.label    = label;
    it.on_click = [this, vk] { InjectKey(vk); };
    return it;
}

JornadaKeysWidget::MenuSection JornadaKeysWidget::KeyRow(
        const JornadaKeyEntry* first, const JornadaKeyEntry* last) {
    MenuSection sec;
    for (const JornadaKeyEntry* k = first; k != last; ++k)
        sec.push_back(MakeKeyItem(k->label, k->vk));
    return sec;
}

std::vector<WidgetMenuItem> JornadaKeysWidget::BuildMenu() {
    std::vector<WidgetMenuItem> items;
    for (auto& sec : MenuSections()) {
        if (sec.empty()) continue;
        if (!items.empty()) items.push_back(WidgetMenuItem{});   /* separator */
        for (auto& it : sec) items.push_back(std::move(it));
    }
    return items;
}

void JornadaKeysWidget::DrawIcon(HDC dc, const RECT& box) const {
    emu_.Get<HostIconCache>().DrawCentered(dc, box, L"ICON_KEYBOARD");
}
