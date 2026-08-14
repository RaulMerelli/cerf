#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* casio_cassiopeia_e55 keybddr.dll 0x14E1FA4 passes this base to sub_14E1EF8
   (VirtualAlloc + VirtualCopy) with size 0x800 at 0x14E1FB0; pcmcia.dll
   0x14C0758 maps the same base with size 0x18 at 0x14C0764. */
constexpr uint32_t kBase = 0x1400A000u;
constexpr uint32_t kSize = 0x800u;

constexpr uint32_t kOffStrap = 0x14u;

/* casio_cassiopeia_e55: D2 clear selects the GIU status source (nk.exe sub_9E814EA8
   @0x9E814EC4, gwes.exe sub_AC2B8 @0xAC2E8). D1 set selects GIUIOSELH 0x1FC0 (nk.exe
   sub_9E816964 @0x9E8169F8, the mask sub_9E8144D4 @0x9E814508 writes at cold boot) and
   the 0xB400E000 card window (pcmcia.dll sub_14C1604). D0: pcmcia.dll 0x14C1524/38. */
constexpr uint16_t kStrapValue = 0x0002u;

class CasioCassiopeiaE55Asic : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioCassiopeiaE55;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kOffStrap) return kStrapValue;
        return Peripheral::ReadHalf(addr);
    }
};

}  /* namespace */

REGISTER_SERVICE(CasioCassiopeiaE55Asic);
