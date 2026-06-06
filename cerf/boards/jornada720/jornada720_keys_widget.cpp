#include "../../core/cerf_emulator.h"
#include "../../host/host_widget.h"
#include "../../host/host_widget_registry.h"
#include "../board_detector.h"
#include "jornada720_keyboard.h"
#include "jornada720_touch.h"

#include <cstdint>

namespace {

/* Labels = the app [HKLM\Software\Microsoft\Shell\Keys] binds to each VK
   (ROM default.reg); the VKs ride the normal MCU-scancode keyboard path. */
struct KeyEntry { const wchar_t* label; uint8_t vk; };
constexpr KeyEntry kAppRow[] = {
    { L"Internet Explorer", 0xC1 }, { L"E-mail",         0xC2 },
    { L"Voice Recorder",    0xC3 }, { L"Contacts",       0xC4 },
    { L"Tasks",             0xC5 }, { L"Calendar",       0xC6 },
    { L"HP quick view",     0xC7 }, { L"HP quick pad",   0xC8 },
    { L"Pocket Access",     0xC9 }, { L"Pocket Excel",   0xCA },
    { L"Pocket Word",       0xCB },
};
constexpr KeyEntry kMediaKeys[] = {
    { L"Volume up", 0xD1 }, { L"Volume down", 0xD2 }, { L"Play / pause", 0xD3 },
};

/* Bezel soft buttons: raw-ADC zone centers per touch.dll sub_FB1314 (gate
   X 31..67; Y zones 151-274 / 361-469 / 591-689 / 815-915; release in the
   zone injects Win+0xCC/CD/CE/D0). Labels = the bound apps in default.reg. */
struct BezelEntry { const wchar_t* label; uint16_t adc_y; };
constexpr uint16_t kBezelAdcX = 49;
constexpr BezelEntry kBezel[] = {
    { L"Settings (bezel 1)",     212 },
    { L"HP backup (bezel 2)",    415 },
    { L"HP dialer (bezel 3)",    640 },
    { L"Media player (bezel 4)", 865 },
};

class Jornada720KeysWidget : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }
    void OnReady() override {
        emu_.Get<HostWidgetRegistry>().Register(this);
    }

    std::wstring WidgetName() const override { return L"Jornada keys"; }
    WidgetGroup  Group() const override { return WidgetGroup::InputControl; }
    std::wstring Tooltip() const override {
        return L"Jornada app / bezel keys — right-click for the key menu";
    }

    std::vector<WidgetMenuItem> BuildMenu() override {
        std::vector<WidgetMenuItem> items;
        for (const auto& k : kAppRow)    items.push_back(KeyItem(k));
        items.push_back(WidgetMenuItem{});               /* separator */
        for (const auto& k : kMediaKeys) items.push_back(KeyItem(k));
        items.push_back(WidgetMenuItem{});               /* separator */
        for (const auto& b : kBezel) {
            WidgetMenuItem it;
            it.label    = b.label;
            it.on_click = [this, y = b.adc_y] {
                emu_.Get<Jornada720Touch>().TapRawAdc(kBezelAdcX, y);
            };
            items.push_back(std::move(it));
        }
        return items;
    }

    void DrawIcon(HDC dc, const RECT& box) const override {
        const int cx = (box.left + box.right) / 2;
        const int cy = (box.top + box.bottom) / 2;
        constexpr int kW = 18, kH = 12;
        const RECT body = { cx - kW / 2, cy - kH / 2,
                            cx + kW / 2, cy + kH / 2 };

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

private:
    WidgetMenuItem KeyItem(const KeyEntry& k) {
        WidgetMenuItem it;
        it.label    = k.label;
        it.on_click = [this, vk = k.vk] {
            auto& kbd = emu_.Get<Jornada720Keyboard>();
            kbd.OnHostKey(vk, /*key_up=*/false);
            kbd.OnHostKey(vk, /*key_up=*/true);
        };
        return it;
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada720KeysWidget);
