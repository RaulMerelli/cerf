#include "wm9705_codec.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"

namespace {
/* AC'97 vendor/device-ID registers + WM9705 identity. Values from the Linux
   wm97xx driver (include/linux/wm97xx.h: WM97XX_ID1 0x574d, WM9705_ID2 0x4c05)
   and confirmed against touch.dll sub_18E1CE8, which reads regs 0x7C/0x7E and
   requires vendor 0x574D + device 0x4C05 (WM9705) to enable the touchscreen. */
constexpr uint32_t kRegVendorId1 = 0x7Cu;
constexpr uint32_t kRegVendorId2 = 0x7Eu;
constexpr uint16_t kWm97xxId1    = 0x574Du;
constexpr uint16_t kWm9705Id2    = 0x4C05u;
}  // namespace

bool Wm9705Codec::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::FalconPC3xx;
}

void Wm9705Codec::OnReady() {
    reg_[kRegVendorId1] = kWm97xxId1;
    reg_[kRegVendorId2] = kWm9705Id2;
}

uint16_t Wm9705Codec::ReadReg(uint32_t reg) {
    return reg < kNumRegs ? reg_[reg] : 0u;
}

void Wm9705Codec::WriteReg(uint32_t reg, uint16_t value) {
    if (reg < kNumRegs) reg_[reg] = value;
}

REGISTER_SERVICE_AS(Wm9705Codec, Ac97Codec);
