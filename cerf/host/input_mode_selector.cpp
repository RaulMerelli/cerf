#define NOMINMAX

#include "input_mode_selector.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "host_widget_registry.h"
#include "touch_input.h"

REGISTER_SERVICE(InputModeSelector);

bool InputModeSelector::ShouldRegister() {
    if (!emu_.Get<DeviceConfig>().guest_additions) return false;
    return emu_.TryGet<TouchInput>() != nullptr;
}

void InputModeSelector::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

std::wstring InputModeSelector::Tooltip() const {
    return Mode() == InputMode::Pointer
        ? L"Input: absolute pointer — click to switch to touch/stylus"
        : L"Input: touch/stylus — click to switch to absolute pointer";
}

void InputModeSelector::OnPrimaryAction() {
    SetMode(Mode() == InputMode::Pointer ? InputMode::Touch : InputMode::Pointer);
}

std::vector<WidgetMenuItem> InputModeSelector::BuildMenu() {
    const InputMode m = Mode();
    std::vector<WidgetMenuItem> items;

    WidgetMenuItem pointer;
    pointer.label    = L"Absolute pointer (mouse)";
    pointer.checked  = m == InputMode::Pointer;
    pointer.on_click = [this] { SetMode(InputMode::Pointer); };
    items.push_back(std::move(pointer));

    WidgetMenuItem touch;
    touch.label    = L"Touch / stylus (original panel)";
    touch.checked  = m == InputMode::Touch;
    touch.on_click = [this] { SetMode(InputMode::Touch); };
    items.push_back(std::move(touch));

    return items;
}

void InputModeSelector::DrawIcon(HDC dc, const RECT& box) const {
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;

    const COLORREF body    = RGB(226, 229, 234);
    const COLORREF outline = RGB(96, 100, 106);
    HPEN    pen = CreatePen(PS_SOLID, 1, outline);
    HBRUSH  br  = CreateSolidBrush(body);
    HGDIOBJ op  = SelectObject(dc, pen);
    HGDIOBJ ob  = SelectObject(dc, br);

    if (drawn_mode_ == InputMode::Pointer) {
        /* Cursor arrow, tip up-left. */
        const POINT a[7] = {
            { cx - 4, cy - 7 }, { cx - 4, cy + 5 }, { cx - 1, cy + 2 },
            { cx + 1, cy + 7 }, { cx + 3, cy + 6 }, { cx + 1, cy + 1 },
            { cx + 5, cy + 1 },
        };
        Polygon(dc, a, 7);
    } else {
        /* Thin stylus, tip down-right, above a surface line. */
        const POINT s[5] = {
            { cx + 5, cy + 6 },                       /* tip */
            { cx + 4, cy + 3 }, { cx - 5, cy - 6 },   /* upper edge */
            { cx - 7, cy - 4 }, { cx + 2, cy + 5 },   /* lower edge */
        };
        Polygon(dc, s, 5);
        MoveToEx(dc, cx + 1, cy + 8, nullptr);
        LineTo(dc, cx + 9, cy + 8);
    }

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

bool InputModeSelector::PollDirty() {
    const InputMode m = Mode();
    if (m == drawn_mode_) return false;
    drawn_mode_ = m;
    return true;
}
