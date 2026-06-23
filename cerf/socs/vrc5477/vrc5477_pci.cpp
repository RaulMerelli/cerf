#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

/* NEC VRC5477 external PCI host-bridge memory window (also carries config
   cycles). An unclaimed address MUST read all-ones (PCI master abort = device
   absent); returning 0 reads as a present device with vendor id 0 and mis-drives
   PCI enumeration. Device models are added as the boot demands them. */

namespace {

constexpr uint32_t kPciMemBase = 0x08000000u;   /* BSP_REG_PA_PCI_MEM */
constexpr uint32_t kPciMemSize = 0x08000000u;   /* BSP_REG_PCI_MEMSIZE */

class Vrc5477Pci : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::VR5500;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kPciMemBase; }
    uint32_t MmioSize() const override { return kPciMemSize; }

    uint8_t  ReadByte (uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf (uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord (uint32_t) override { return 0xFFFFFFFFu; }
    void WriteByte(uint32_t, uint8_t)  override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

}  /* namespace */

REGISTER_SERVICE(Vrc5477Pci);
