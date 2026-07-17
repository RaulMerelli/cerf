#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* ROM Area PA 0x18000000-0x1FFFFFFF; ROMCS banks stack down from 0x1FFFFFFF, top
   16 MB populated at PA 0x1F000000 (VR4121 UM Fig 6-8, Table 6-6, Table 6-7).
   RFU below is not illegal-access-notified (UM 11.4.7: only 0x0D000000-0x0FFFFFFF
   and 0x04000000-0x09FFFFFF). Guest reader nk.exe sub_9F0B75F4 case 1 @ PA 0x1D99FFFC. */
constexpr uint32_t kBase = 0x18000000u;
constexpr uint32_t kSize = 0x1F000000u - 0x18000000u;

class CasioToricomailRomOpenBus : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioToricomail;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t  ReadByte (uint32_t addr) override { Trace("r8",  addr); return 0xFFu; }
    uint16_t ReadHalf (uint32_t addr) override { Trace("r16", addr); return 0xFFFFu; }
    uint32_t ReadWord (uint32_t addr) override { Trace("r32", addr); return 0xFFFFFFFFu; }
    uint64_t ReadDword(uint32_t addr) override { Trace("r64", addr); return 0xFFFFFFFFFFFFFFFFull; }
    void WriteByte (uint32_t addr, uint8_t)  override { Trace("w8",  addr); }
    void WriteHalf (uint32_t addr, uint16_t) override { Trace("w16", addr); }
    void WriteWord (uint32_t addr, uint32_t) override { Trace("w32", addr); }
    void WriteDword(uint32_t addr, uint64_t) override { Trace("w64", addr); }

private:
    void Trace(const char* op, uint32_t addr) {
        if (++accesses_ <= 16u) {
            LOG(Periph, "[CasioRomOpenBus] %s 0x%08X (floating ROM-Area RFU)\n", op, addr);
        }
    }

    uint32_t accesses_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(CasioToricomailRomOpenBus);
