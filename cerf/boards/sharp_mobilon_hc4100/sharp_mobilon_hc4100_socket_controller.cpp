#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/ite_it8368/ite_it8368.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* The IT8368E is on chip select 2, PA $1040_0000: pcmcia.dll sub_14913DC
   VirtualCopies kseg1 $B040_0000 into its register pointer, and the CS2 fault
   pa=0x10400020 lands here. */
constexpr uint32_t kBase = 0x10400000u;
constexpr uint32_t kSize = 0x22u;

/* This board crosses the chip's byte lanes: pcmcia.dll sub_149120C writes
   GPIODIR = $F100 for the chip's $00F1 and MFIODIR = $FF07 for $07FF. */
constexpr uint16_t Swap(uint16_t v) {
    return static_cast<uint16_t>((v << 8) | (v >> 8));
}

class SharpMobilonHc4100SocketController : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
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

    uint8_t  ReadByte (uint32_t a) override { HaltUnsupportedAccess("HC-4100 IT8368E ReadByte", a, 0); }
    uint32_t ReadWord (uint32_t a) override { HaltUnsupportedAccess("HC-4100 IT8368E ReadWord", a, 0); }
    uint64_t ReadDword(uint32_t a) override { HaltUnsupportedAccess("HC-4100 IT8368E ReadDword", a, 0); }
    void WriteByte (uint32_t a, uint8_t  v) override { HaltUnsupportedAccess("HC-4100 IT8368E WriteByte", a, v); }
    void WriteWord (uint32_t a, uint32_t v) override { HaltUnsupportedAccess("HC-4100 IT8368E WriteWord", a, v); }
    void WriteDword(uint32_t a, uint64_t v) override { HaltUnsupportedAccess("HC-4100 IT8368E WriteDword", a, v); }

    void SaveState(StateWriter& w) override { emu_.Get<IteIt8368>().SaveState(w); }
    void RestoreState(StateReader& r) override { emu_.Get<IteIt8368>().RestoreState(r); }
};

}  /* namespace */

REGISTER_SERVICE(SharpMobilonHc4100SocketController);
