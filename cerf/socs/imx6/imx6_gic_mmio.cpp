#include "imx6_gic.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

namespace {

class Imx6GicMmio final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return Imx6Gic::kMmioBase; }
    uint32_t MmioSize() const override { return Imx6Gic::kMmioSize; }

    uint8_t ReadByte(uint32_t address) override {
        const uint32_t word = Gic().ReadMmio((address & ~3u) - MmioBase());
        return static_cast<uint8_t>(word >> ((address & 3u) * 8u));
    }

    uint16_t ReadHalf(uint32_t address) override {
        const uint32_t word = Gic().ReadMmio((address & ~3u) - MmioBase());
        return static_cast<uint16_t>(word >> ((address & 2u) * 8u));
    }

    uint32_t ReadWord(uint32_t address) override { return Gic().ReadMmio(address - MmioBase()); }

    void WriteByte(uint32_t address, uint8_t value) override { WriteMerged(address, value, 1u); }

    void WriteHalf(uint32_t address, uint16_t value) override { WriteMerged(address, value, 2u); }

    void WriteWord(uint32_t address, uint32_t value) override { Gic().WriteMmio(address - MmioBase(), value); }

    void SaveState(StateWriter& writer) override { Gic().SaveGicState(writer); }

    void RestoreState(StateReader& reader) override { Gic().RestoreGicState(reader); }

    void PostRestore() override { Gic().PostRestoreGicState(); }

private:
    Imx6Gic& Gic() { return emu_.Get<Imx6Gic>(); }

    void WriteMerged(uint32_t address, uint32_t value, uint32_t width) {
        const uint32_t aligned = address & ~3u;
        const uint32_t shift = (address & 3u) * 8u;
        const uint32_t lane_mask = width == 1u ? 0xFFu : 0xFFFFu;
        const uint32_t old_value = Gic().ReadMmio(aligned - MmioBase());
        Gic().WriteMmio(aligned - MmioBase(), (old_value & ~(lane_mask << shift)) | ((value & lane_mask) << shift));
    }
};

} // namespace

REGISTER_SERVICE(Imx6GicMmio);
