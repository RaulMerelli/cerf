#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* SIMpad bootloader warm-boot/param handoff scratch, probed by the CE boot stub
   (nk.exe 0x80081194): magic 0x87654321 at base+0xC selects a warm-resume path.
   CERF always cold-boots, so returning non-magic (0) sends the stub down its
   cold path. */
class SimpadSl4BootHandoff : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0xAC080300u; }
    uint32_t MmioSize() const override { return 0x00000010u; }

    uint32_t ReadWord(uint32_t addr) override {
        LOG(Board, "SimpadSl4BootHandoff: read 0x%08X -> 0 (cold boot, no "
                   "bootloader handoff)\n", addr);
        return 0u;
    }
};

}  /* namespace */

REGISTER_SERVICE(SimpadSl4BootHandoff);
