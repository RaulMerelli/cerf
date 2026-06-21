#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_hotkey_menu.h"
#include "../board_detector.h"
#include "jornada820_keyboard.h"

#include <cstdint>
#include <vector>

namespace {

struct KeyEntry { const wchar_t* label; uint8_t vk; };

/* Physical J820 app-key row, left-to-right (device-owner photo; 0xC1 Inbox /
   0xC2 IE / 0xC3 Calendar runtime-confirmed). They ride the normal scancode
   path. NOT the J720 row - copying that mislabels every hardware key. */
constexpr KeyEntry kAppRow[] = {
    { L"Inbox",        0xC1 }, { L"Internet Explorer", 0xC2 },
    { L"Calendar",     0xC3 }, { L"Contacts",          0xC4 },
    { L"Tasks",        0xC5 }, { L"Pocket Word",       0xC6 },
    { L"Pocket Excel", 0xC7 }, { L"Pocket PowerPoint", 0xC8 },
    { L"HP viewer",    0xC9 }, { L"Voice Recorder",    0xCA },
    { L"HP settings",  0xCB },
};

class Jornada820KeyboardHotkeyMenu : public KeyboardHotkeyMenu {
public:
    using KeyboardHotkeyMenu::KeyboardHotkeyMenu;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

    std::vector<MenuSection> HotkeySections() override {
        MenuSection sec;
        for (const KeyEntry& k : kAppRow) {
            WidgetMenuItem it;
            it.label    = k.label;
            it.on_click = [this, vk = k.vk] {
                auto& kbd = emu_.Get<Jornada820Keyboard>();
                kbd.OnHostKey(vk, /*key_up=*/false);
                kbd.OnHostKey(vk, /*key_up=*/true);
            };
            sec.push_back(std::move(it));
        }
        return { std::move(sec) };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820KeyboardHotkeyMenu, KeyboardHotkeyMenu);
