#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
namespace {
class Iop13xxSecondaryAtuStatus final : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::IOP13xx;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }
    uint32_t MmioBase() const override { return 0xFFDC8000u; }
    uint32_t MmioSize() const override { return 0x10u; }
    uint32_t ReadWord(uint32_t) override { return 0u; }
    uint16_t ReadHalf(uint32_t) override { return 0u; }
    uint8_t ReadByte(uint32_t) override { return 0u; }
    void WriteWord(uint32_t, uint32_t) override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteByte(uint32_t, uint8_t) override {}
};
} // namespace
REGISTER_SERVICE(Iop13xxSecondaryAtuStatus);
