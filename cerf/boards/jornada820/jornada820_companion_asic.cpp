#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <vector>

namespace {

/* HP Jornada 820 companion ASIC on nCS3 (PA 0x18000000): the display/keyboard/
   glidepad controller. Storage-backed because the kernel's pre-MMU bring-up
   (nk.exe sub_8003970C) read-modify-writes its control registers; per-register
   device behavior is added as the ddi/keybddr/glidepad drivers exercise it. */
class Jornada820CompanionAsic : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }
    void OnReady() override {
        store_.assign(MmioSize(), 0u);
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x18000000u; }
    uint32_t MmioSize() const override { return 0x00400000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return store_[addr - MmioBase()];
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t o = addr - MmioBase();
        return static_cast<uint16_t>(store_[o] | (store_[o + 1] << 8));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t o = addr - MmioBase();
        return static_cast<uint32_t>(store_[o]) | (store_[o + 1] << 8) |
               (store_[o + 2] << 16) | (store_[o + 3] << 24);
    }

    void WriteByte(uint32_t addr, uint8_t v) override {
        store_[addr - MmioBase()] = v;
    }
    void WriteHalf(uint32_t addr, uint16_t v) override {
        const uint32_t o = addr - MmioBase();
        store_[o]     = static_cast<uint8_t>(v);
        store_[o + 1] = static_cast<uint8_t>(v >> 8);
    }
    void WriteWord(uint32_t addr, uint32_t v) override {
        const uint32_t o = addr - MmioBase();
        store_[o]     = static_cast<uint8_t>(v);
        store_[o + 1] = static_cast<uint8_t>(v >> 8);
        store_[o + 2] = static_cast<uint8_t>(v >> 16);
        store_[o + 3] = static_cast<uint8_t>(v >> 24);
    }

private:
    std::vector<uint8_t> store_;
};

}  /* namespace */

REGISTER_SERVICE(Jornada820CompanionAsic);
