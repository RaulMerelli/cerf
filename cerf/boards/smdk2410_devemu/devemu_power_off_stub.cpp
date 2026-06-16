#include "../board_detector.h"

#include "../../boot/board_boot_placer.h"
#include "../../boot/guest_cold_boot.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"

#include <cstdint>

namespace {

/* CPUPowerOff (cpupoweroff.s) tests flash[0]: a NOR signature (0xEA0003FE) jumps
   to offset 0x1004, otherwise (NAND) to offset 0x4. RomPlacer leaves the flash
   erased (0xFF), so the guest takes the NAND branch and the stub lives at offset
   0x4. Planted from PlaceAfterRom so it lands AFTER RomPlacer's 0xFF erase. */
constexpr uint32_t kStubFlashPa = 0x4u;
constexpr uint32_t kStub[] = {
    0xE5801000u,  /* STR R1,[R0] — REFRESH: SDRAM self-refresh  */
    0xE5823000u,  /* STR R3,[R2] — MISCCR                       */
    0xE5845000u,  /* STR R5,[R4] — CLKCON <- 0x7fff8 (power off) */
    0xEAFFFFFEu,  /* B .                                        */
};

class DevEmuPowerOffStub : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void OnReady() override {
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { WriteStub(); });
    }

    /* RomPlacer calls this after erasing the flash region to 0xFF. */
    void PlaceAfterRom() override { WriteStub(); }

private:
    void WriteStub() {
        emu_.Get<EmulatedMemory>().CopyIn(kStubFlashPa, kStub, sizeof(kStub));
        LOG(Board, "DevEmuPowerOffStub: power-off stub planted at flash offset "
            "0x%X (NAND power-off target 0x88000004)\n", kStubFlashPa);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(DevEmuPowerOffStub, BoardBootPlacer);
