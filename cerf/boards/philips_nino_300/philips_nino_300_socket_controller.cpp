#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/ite_it8368/ite_it8368.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* The IT8368E sits on chip select 2, PA $1040_0000 (Table 4.2.1, TMPR3911.pdf
   PDF p.103). pcmcia_mips.dll sub_18811D0 VirtualCopies 34 bytes of it. */
constexpr uint32_t kBase = 0x10400000u;
constexpr uint32_t kSize = 0x22u;

/* The chip's byte lanes are crossed on this board's bus: nk.exe sub_9F411C5C
   writes CTRL = $0B80 for $800B and GPIODIR = $F100 for $00F1. */
constexpr uint16_t Swap(uint16_t v) {
    return static_cast<uint16_t>((v << 8) | (v >> 8));
}

class PhilipsNino300SocketController : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsNino300;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        return Swap(emu_.Get<IteIt8368>().ReadReg(addr - kBase));
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        emu_.Get<IteIt8368>().WriteReg(addr - kBase, Swap(value));
    }

    uint8_t  ReadByte (uint32_t a) override { HaltUnsupportedAccess("Nino IT8368E ReadByte", a, 0); }
    uint32_t ReadWord (uint32_t a) override { HaltUnsupportedAccess("Nino IT8368E ReadWord", a, 0); }
    uint64_t ReadDword(uint32_t a) override { HaltUnsupportedAccess("Nino IT8368E ReadDword", a, 0); }
    void WriteByte (uint32_t a, uint8_t  v) override { HaltUnsupportedAccess("Nino IT8368E WriteByte", a, v); }
    void WriteWord (uint32_t a, uint32_t v) override { HaltUnsupportedAccess("Nino IT8368E WriteWord", a, v); }
    void WriteDword(uint32_t a, uint64_t v) override { HaltUnsupportedAccess("Nino IT8368E WriteDword", a, v); }

    void SaveState(StateWriter& w) override { emu_.Get<IteIt8368>().SaveState(w); }
    void RestoreState(StateReader& r) override { emu_.Get<IteIt8368>().RestoreState(r); }
};

}  /* namespace */

REGISTER_SERVICE(PhilipsNino300SocketController);
