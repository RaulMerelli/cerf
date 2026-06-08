#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* The 820 populates only DRAM bank 0; the SA-1100 extension banks (PA
   0xC8/0xD0/0xD8000000) are empty. The kernel's RAM-sizing probe (nk.exe
   sub_8005A188, via the uncached 0xA0000000 alias) detects empty by readback
   mismatch, so these must read open-bus 0xFF and must not store the pattern. */
class Jornada820UnpopulatedDram : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0xC8000000u; }
    uint32_t MmioSize() const override { return 0x18000000u; }

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }

    void WriteByte(uint32_t, uint8_t)  override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

}  /* namespace */

REGISTER_SERVICE(Jornada820UnpopulatedDram);
