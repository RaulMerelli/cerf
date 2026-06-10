#include "../jornada/jornada_keys_widget.h"

#include "../../core/cerf_emulator.h"
#include "../board_detector.h"
#include "jornada820_keyboard.h"

#include <cstdint>

namespace {

/* Order = the physical J820 key row left-to-right (device-owner photo; 0xC1
   Inbox / 0xC2 IE / 0xC3 Calendar runtime-confirmed). NOT the J720 row —
   copying that layout mislabels every hardware key. */
constexpr JornadaKeyEntry kAppRow[] = {
    { L"Inbox",              0xC1 }, { L"Internet Explorer", 0xC2 },
    { L"Calendar",           0xC3 }, { L"Contacts",          0xC4 },
    { L"Tasks",              0xC5 }, { L"Pocket Word",       0xC6 },
    { L"Pocket Excel",       0xC7 }, { L"Pocket PowerPoint", 0xC8 },
    { L"HP viewer",          0xC9 }, { L"Voice Recorder",    0xCA },
    { L"HP settings",        0xCB },
};

class Jornada820KeysWidget : public JornadaKeysWidget {
public:
    using JornadaKeysWidget::JornadaKeysWidget;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

protected:
    std::vector<MenuSection> MenuSections() override {
        return { KeyRow(std::begin(kAppRow), std::end(kAppRow)) };
    }
    void InjectKey(uint8_t vk) override {
        auto& kbd = emu_.Get<Jornada820Keyboard>();
        kbd.OnHostKey(vk, /*key_up=*/false);
        kbd.OnHostKey(vk, /*key_up=*/true);
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada820KeysWidget);
