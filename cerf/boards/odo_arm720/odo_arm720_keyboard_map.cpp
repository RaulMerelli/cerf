#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_map.h"
#include "../board_detector.h"

#include <cstdint>
#include <vector>

namespace {

/* Win32 VK -> PS/2 Set 2 scancode; device_code = scancode | extended<<8.
   Standard PS/2 Set 2 — kernel ScanCodeToVKeyEx (SCTOVK.CPP) is the reverse map;
   deviating from Set 2 shuffles all keys. */
struct ScancodeEntry { uint8_t scancode; bool extended; };

constexpr ScancodeEntry MakePlain(uint8_t sc) { return { sc, false }; }
constexpr ScancodeEntry MakeExt  (uint8_t sc) { return { sc, true  }; }
constexpr ScancodeEntry MakeNone() { return { 0, false }; }

constexpr ScancodeEntry kVkToPs2Set2[256] = {
    /* 0x00..0x07 */ MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    /* 0x08 VK_BACK    */ MakePlain(0x66),
    /* 0x09 VK_TAB     */ MakePlain(0x0D),
    /* 0x0A..0x0C */ MakeNone(), MakeNone(), MakeNone(),
    /* 0x0D VK_RETURN  */ MakePlain(0x5A),
    /* 0x0E..0x0F */ MakeNone(), MakeNone(),
    /* 0x10 VK_SHIFT   */ MakePlain(0x12),
    /* 0x11 VK_CONTROL */ MakePlain(0x14),
    /* 0x12 VK_MENU    */ MakePlain(0x11),
    /* 0x13..0x1A */ MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    /* 0x1B VK_ESCAPE  */ MakePlain(0x76),
    /* 0x1C..0x1F */ MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    /* 0x20 VK_SPACE   */ MakePlain(0x29),
    /* 0x21 VK_PRIOR   */ MakeExt(0x7D),
    /* 0x22 VK_NEXT    */ MakeExt(0x7A),
    /* 0x23 VK_END     */ MakeExt(0x69),
    /* 0x24 VK_HOME    */ MakeExt(0x6C),
    /* 0x25 VK_LEFT    */ MakeExt(0x6B),
    /* 0x26 VK_UP      */ MakeExt(0x75),
    /* 0x27 VK_RIGHT   */ MakeExt(0x74),
    /* 0x28 VK_DOWN    */ MakeExt(0x72),
    /* 0x29..0x2C */ MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    /* 0x2D VK_INSERT  */ MakeExt(0x70),
    /* 0x2E VK_DELETE  */ MakeExt(0x71),
    /* 0x2F */ MakeNone(),
    /* 0x30..0x39 '0'..'9' */
    MakePlain(0x45), MakePlain(0x16), MakePlain(0x1E), MakePlain(0x26),
    MakePlain(0x25), MakePlain(0x2E), MakePlain(0x36), MakePlain(0x3D),
    MakePlain(0x3E), MakePlain(0x46),
    /* 0x3A..0x40 */ MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(), MakeNone(), MakeNone(),
    /* 0x41..0x5A 'A'..'Z' */
    MakePlain(0x1C), MakePlain(0x32), MakePlain(0x21), MakePlain(0x23),
    MakePlain(0x24), MakePlain(0x2B), MakePlain(0x34), MakePlain(0x33),
    MakePlain(0x43), MakePlain(0x3B), MakePlain(0x42), MakePlain(0x4B),
    MakePlain(0x3A), MakePlain(0x31), MakePlain(0x44), MakePlain(0x4D),
    MakePlain(0x15), MakePlain(0x2D), MakePlain(0x1B), MakePlain(0x2C),
    MakePlain(0x3C), MakePlain(0x2A), MakePlain(0x1D), MakePlain(0x22),
    MakePlain(0x35), MakePlain(0x1A),
    /* 0x5B..0x6F */ MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(), MakeNone(), MakeNone(), MakeNone(),
                     MakeNone(),
    /* 0x70..0x7B F1..F12 */
    MakePlain(0x05), MakePlain(0x06), MakePlain(0x04), MakePlain(0x0C),
    MakePlain(0x03), MakePlain(0x0B), MakePlain(0x83), MakePlain(0x0A),
    MakePlain(0x01), MakePlain(0x09), MakePlain(0x78), MakePlain(0x07),
    /* 0x7C..0x8B */
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    /* 0x8C..0x9F */
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    /* 0xA0..0xFF */
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(), MakeNone(), MakeNone(),
    MakeNone(), MakeNone(),
};

class OdoArm720KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::OdoArm720;
    }

    void OnReady() override {
        for (int vk = 0; vk < 256; ++vk) {
            const ScancodeEntry& e = kVkToPs2Set2[vk];
            if (e.scancode == 0) continue;
            const uint32_t code =
                static_cast<uint32_t>(e.scancode) | (e.extended ? 0x100u : 0u);
            bindings_.push_back({ static_cast<uint8_t>(vk), code, nullptr, 0, 0 });
        }
    }

    const std::vector<KeyBinding>& Bindings() const override { return bindings_; }

private:
    std::vector<KeyBinding> bindings_;
};

}  /* namespace */

REGISTER_SERVICE_AS(OdoArm720KeyboardMap, KeyboardMap);
