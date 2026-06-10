#include "../jornada/jornada_keys_widget.h"

#include "../../core/cerf_emulator.h"
#include "../board_detector.h"
#include "jornada720_keyboard.h"
#include "jornada720_touch.h"

#include <cstdint>

namespace {

/* Labels = the app [HKLM\Software\Microsoft\Shell\Keys] binds to each VK
   (ROM default.reg); the VKs ride the normal MCU-scancode keyboard path. */
constexpr JornadaKeyEntry kAppRow[] = {
    { L"Internet Explorer", 0xC1 }, { L"E-mail",         0xC2 },
    { L"Voice Recorder",    0xC3 }, { L"Contacts",       0xC4 },
    { L"Tasks",             0xC5 }, { L"Calendar",       0xC6 },
    { L"HP quick view",     0xC7 }, { L"HP quick pad",   0xC8 },
    { L"Pocket Access",     0xC9 }, { L"Pocket Excel",   0xCA },
    { L"Pocket Word",       0xCB },
};
constexpr JornadaKeyEntry kMediaKeys[] = {
    { L"Volume up", 0xD1 }, { L"Volume down", 0xD2 }, { L"Play / pause", 0xD3 },
};

/* The Fn modifier is guest VK 0x79 (TSCkbdr.dll sub_FC2CE8 case 121 toggles the
   Fn-active flag dword_FC5124; sub_FC2384 then selects word_FC10A0 col7). Host
   F10 already reaches it via kVkToScancode[0x79]. */
constexpr uint8_t kVkFn = 0x79;

/* Fn-layer symbols (word_FC10A0 col7) that have no plain key on the J720:
   label + the base VK whose Fn column carries the symbol. */
struct FnSymEntry { const wchar_t* label; uint8_t base_vk; };
constexpr FnSymEntry kFnSymbols[] = {
    { L"{   (Fn + P)",  0x50 },   /* word_FC10A0 VK 0x50 col7 = 0x7B '{' */
    { L"}   (Fn + \\)", 0xDC },   /* VK 0xDC col7 = 0x7D '}' */
    { L"[   (Fn + ;)",  0xBA },   /* VK 0xBA col7 = 0x5B '[' */
    { L"]   (Fn + ')",  0xDE },   /* VK 0xDE col7 = 0x5D ']' */
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

class Jornada720KeysWidget : public JornadaKeysWidget {
public:
    using JornadaKeysWidget::JornadaKeysWidget;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }

protected:
    std::vector<MenuSection> MenuSections() override {
        return {
            FnHintSection(),
            FnSymbolSection(),
            KeyRow(std::begin(kAppRow),    std::end(kAppRow)),
            KeyRow(std::begin(kMediaKeys), std::end(kMediaKeys)),
            BezelSection(),
        };
    }
    void InjectKey(uint8_t vk) override {
        auto& kbd = emu_.Get<Jornada720Keyboard>();
        kbd.OnHostKey(vk, /*key_up=*/false);
        kbd.OnHostKey(vk, /*key_up=*/true);
    }

private:
    MenuSection FnHintSection() {
        WidgetMenuItem hint;
        hint.label   = L"Hint: F10 is mapped to guest's Fn key";
        hint.enabled = false;                            /* grayed static header */
        MenuSection sec;
        sec.push_back(std::move(hint));
        return sec;
    }
    MenuSection FnSymbolSection() {
        MenuSection sec;
        for (const auto& s : kFnSymbols) {
            WidgetMenuItem it;
            it.label    = s.label;
            it.on_click = [this, vk = s.base_vk] { InjectFnCombo(vk); };
            sec.push_back(std::move(it));
        }
        return sec;
    }
    MenuSection BezelSection() {
        MenuSection sec;
        for (const auto& b : kBezel) {
            WidgetMenuItem it;
            it.label    = b.label;
            it.on_click = [this, y = b.adc_y] {
                emu_.Get<Jornada720Touch>().TapRawAdc(kBezelAdcX, y);
            };
            sec.push_back(std::move(it));
        }
        return sec;
    }

    /* Hold-Fn + tap base + release-Fn, mirroring the verified host-F10 path:
       Fn-down latches the Fn layer, the base key resolves through col7, Fn-up
       clears it. */
    void InjectFnCombo(uint8_t base_vk) {
        auto& kbd = emu_.Get<Jornada720Keyboard>();
        kbd.OnHostKey(kVkFn,   /*key_up=*/false);
        kbd.OnHostKey(base_vk, /*key_up=*/false);
        kbd.OnHostKey(base_vk, /*key_up=*/true);
        kbd.OnHostKey(kVkFn,   /*key_up=*/true);
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada720KeysWidget);
