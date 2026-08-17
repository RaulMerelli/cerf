#include "iop13xx_cp6.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/irq_controller.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

class Iop13xxPmmrGuard : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetSoc() == SocFamily::IOP13xx;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t ReadWord(uint32_t addr) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR word read", addr, 0);
    }

    uint16_t ReadHalf(uint32_t addr) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR halfword read", addr, 0);
    }

    uint8_t ReadByte(uint32_t addr) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR byte read", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR word write", addr, value);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR halfword write", addr, value);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR byte write", addr, value);
    }
};

class Iop13xxPmmrLow final : public Iop13xxPmmrGuard {
public:
    using Iop13xxPmmrGuard::Iop13xxPmmrGuard;
    uint32_t MmioBase() const override { return 0xFFD80000u; }
    uint32_t MmioSize() const override { return 0x00002340u; }

    void SaveState(StateWriter& writer) override {
        static_cast<Iop13xxCp6&>(emu_.Get<IrqController>()).SaveState(writer);
    }

    void RestoreState(StateReader& reader) override {
        static_cast<Iop13xxCp6&>(emu_.Get<IrqController>()).RestoreState(reader);
    }

    void PostRestore() override {
        static_cast<Iop13xxCp6&>(emu_.Get<IrqController>()).PostRestoreState();
    }
};

#define IOP13XX_PMMR_GUARD(name, base, size)                 \
    class name final : public Iop13xxPmmrGuard {            \
    public:                                                   \
        using Iop13xxPmmrGuard::Iop13xxPmmrGuard;           \
        uint32_t MmioBase() const override { return base; }  \
        uint32_t MmioSize() const override { return size; }  \
    }

IOP13XX_PMMR_GUARD(Iop13xxPmmrAfterUart, 0xFFD82370u, 0x00000110u);
IOP13XX_PMMR_GUARD(Iop13xxPmmrAfterGpio, 0xFFD8248Cu, 0x00000074u);
IOP13XX_PMMR_GUARD(Iop13xxPmmrAfterI2c, 0xFFD82518u, 0x00045AE8u);
IOP13XX_PMMR_GUARD(Iop13xxPmmrAfterSecondaryStatus, 0xFFDC8010u, 0x0000031Cu);
IOP13XX_PMMR_GUARD(Iop13xxPmmrAfterSecondaryConfig, 0xFFDC8334u, 0x00004CCCu);
IOP13XX_PMMR_GUARD(Iop13xxPmmrAtuGap, 0xFFDCD100u, 0x00000200u);
IOP13XX_PMMR_GUARD(Iop13xxPmmrHigh, 0xFFDCD338u, 0x000B2CC8u);

#undef IOP13XX_PMMR_GUARD

REGISTER_SERVICE(Iop13xxPmmrLow);
REGISTER_SERVICE(Iop13xxPmmrAfterUart);
REGISTER_SERVICE(Iop13xxPmmrAfterGpio);
REGISTER_SERVICE(Iop13xxPmmrAfterI2c);
REGISTER_SERVICE(Iop13xxPmmrAfterSecondaryStatus);
REGISTER_SERVICE(Iop13xxPmmrAfterSecondaryConfig);
REGISTER_SERVICE(Iop13xxPmmrAtuGap);
REGISTER_SERVICE(Iop13xxPmmrHigh);

}

