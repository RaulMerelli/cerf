#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* NEC MobilePro 700 board register in the VR4102 ISA-bus I/O space (range per UM
   Table 5-6). nk.exe start() (0x9F001C40) reads 0x15001080 bit 3: SET = cold boot,
   CLEAR = HIBERNATE (cop0 0x23 -> FATAL). CERF cold-boots, so it reads bit 3 SET. */
constexpr uint32_t kBase      = 0x15001080u;
constexpr uint32_t kSize      = 0x04u;
constexpr uint32_t kOffStatus = 0x00u;   /* 0x15001080: bit 3 = resume flag */
constexpr uint32_t kOffCtrl   = 0x02u;   /* 0x15001082: control latch */

class NecMobilePro700IsaSysReg : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        if (addr - kBase == kOffStatus) return 0x08u;  /* bit 3 SET = boot path (clear = HIBERNATE) */
        HaltUnsupportedAccess("IsaSysReg ReadByte", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        if (addr - kBase == kOffCtrl) { ctrl_ = value; return; }
        HaltUnsupportedAccess("IsaSysReg WriteByte", addr, value);
    }

    uint16_t ReadHalf (uint32_t addr) override { HaltUnsupportedAccess("IsaSysReg ReadHalf", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("IsaSysReg ReadWord", addr, 0); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("IsaSysReg WriteHalf", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("IsaSysReg WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(ctrl_); }
    void RestoreState(StateReader& r) override { r.Read(ctrl_); }

private:
    uint8_t ctrl_ = 0;   /* 0x15001082 control latch */
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro700IsaSysReg);
