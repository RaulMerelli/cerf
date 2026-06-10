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
    using MenuSection = std::vector<WidgetMenuItem>;

    /* Ordered menu sections; BuildMenu joins each non-empty section with a
       separator (earlier sections render higher). A new block is one more entry
       in the returned list — no new hook. */
    virtual std::vector<MenuSection> MenuSections() = 0;

    virtual void InjectKey(uint8_t vk) = 0;            /* tap (down+up) via the board keyboard */

    /* One app-launch item / a section of them that InjectKey their VK. */
    WidgetMenuItem MakeKeyItem(const wchar_t* label, uint8_t vk);
    MenuSection    KeyRow(const JornadaKeyEntry* first, const JornadaKeyEntry* last);
};
