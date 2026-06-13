#define NOMINMAX

#include "input_mode_selector.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "host_icon_cache.h"
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
    emu_.Get<HostIconCache>().DrawCentered(
        dc, box, Mode() == InputMode::Pointer ? L"ICON_INPUT_POINTER"
                                              : L"ICON_INPUT_TOUCH");
}

bool InputModeSelector::PollDirty() {
    const InputMode m = Mode();
    if (m == drawn_mode_) return false;
    drawn_mode_ = m;
    return true;
}
