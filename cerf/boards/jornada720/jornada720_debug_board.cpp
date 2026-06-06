#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* HP Jornada 720 optional debug board (Cirrus CL-CD1284 parallel port) at
   PA 0x1A000000. Hardware doc §2: "ignore if you don't have debug board".
   The emulated unit ships without it, so this models the absent device:
   reads float (0xFF), writes drop. KernelStart writes boot-progress codes
   to offset 0x400. */
class Jornada720DebugBoard : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x1A000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }

    void WriteByte(uint32_t addr, uint8_t  v) override { LogProgress(addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { LogProgress(addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { LogProgress(addr, v); }

private:
    void LogProgress(uint32_t addr, uint32_t v) {
        if (addr - MmioBase() == 0x400u) {
            LOG(Boot, "[J720] firmware boot-progress code 0x%02X\n", v & 0xFFu);
        }
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada720DebugBoard);
