#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"

#include <cstdint>
#include <string>
#include <vector>

struct JornadaKeyEntry { const wchar_t* label; uint8_t vk; };

/* Shared HP Jornada hardware-keys host widget: a status-bar button whose
   right-click menu taps the machine's app-launch / media hotkeys (VK 0xC1.. ride
   the normal keyboard-scancode path). Concretes supply the key list, the
   keyboard injection, and any board-only extras (e.g. the 720 bezel buttons). */
class JornadaKeysWidget : public Service, public HostWidget {
public:
    using Service::Service;
    void OnReady() override;

    std::wstring WidgetName() const override { return L"Jornada keys"; }
    WidgetGroup  Group() const override { return WidgetGroup::InputControl; }
    std::wstring Tooltip() const override {
        return L"Jornada hardware keys — right-click for the key menu";
    }
    void DrawIcon(HDC dc, const RECT& box) const override;
    std::vector<WidgetMenuItem> BuildMenu() override;

protected:
    virtual std::vector<JornadaKeyEntry> AppKeys() const = 0;
    virtual void InjectKey(uint8_t vk) = 0;            /* tap (down+up) via the board keyboard */
    virtual std::vector<WidgetMenuItem> ExtraMenuItems() { return {}; }

    WidgetMenuItem MakeKeyItem(const wchar_t* label, uint8_t vk);
};
