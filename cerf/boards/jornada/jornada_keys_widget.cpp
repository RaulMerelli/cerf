#include "jornada_keys_widget.h"

#include "../../core/cerf_emulator.h"
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
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    constexpr int kW = 18, kH = 12;
    const RECT body = { cx - kW / 2, cy - kH / 2, cx + kW / 2, cy + kH / 2 };

    HPEN    pen  = CreatePen(PS_SOLID, 1, RGB(170, 170, 180));
    HBRUSH  dark = CreateSolidBrush(RGB(30, 32, 38));
    HGDIOBJ op   = SelectObject(dc, pen);
    HGDIOBJ ob   = SelectObject(dc, dark);
    Rectangle(dc, body.left, body.top, body.right, body.bottom);

    HBRUSH key = CreateSolidBrush(RGB(150, 160, 170));
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            RECT k = { body.left + 2 + col * 5, body.top + 2 + row * 5,
                       body.left + 5 + col * 5, body.top + 5 + row * 5 };
            FillRect(dc, &k, key);
        }
    }
    DeleteObject(key);

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(dark);
    DeleteObject(pen);
}
