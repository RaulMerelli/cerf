#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/ite_it8368/ite_it8368.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* The M-Module's IT8368E sits on chip select 2, PA $1040_0000 (Table 4.2.1,
   TMPR3911.pdf PDF p.104). pcmcia.dll sub_1F113A0 VirtualCopies $24 bytes of it. */
constexpr uint32_t kBase = 0x10400000u;
constexpr uint32_t kSize = 0x24u;

/* $22 is past the IT8368E's last register (it8368reg.h stops at IT8368_CTRL_REG
   $20): it is the M-Module's own ID. nk.exe sub_9F40F688 programs the socket only
   when (id & 0xFF00) == 0x4900, and nk.1.exe sub_9FB11194 needs exactly 0x4900. */
constexpr uint32_t kOffModuleId = 0x22u;
constexpr uint16_t kIcebergId   = 0x4900u;

class PhilipsVelo1SocketController : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsVelo1;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    /* This board does not cross the chip's byte lanes, so the halfword reaches the
       IT8368E in its own bit order: nk.exe sub_9F40F688 writes GPIODIR = $10F1,
       which is it8368.c:262-265's CRDVCC|CRDVPP|BCRDRST plus CRDSW. */
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off == kOffModuleId) return kIcebergId;
        return emu_.Get<IteIt8368>().ReadReg(off);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - kBase;
        if (off == kOffModuleId) {
            HaltUnsupportedAccess("Velo 1 M-Module ID write", addr, value);
        }
        emu_.Get<IteIt8368>().WriteReg(off, value);
    }

    uint8_t  ReadByte (uint32_t a) override { HaltUnsupportedAccess("Velo 1 IT8368E ReadByte", a, 0); }
    uint32_t ReadWord (uint32_t a) override { HaltUnsupportedAccess("Velo 1 IT8368E ReadWord", a, 0); }
    uint64_t ReadDword(uint32_t a) override { HaltUnsupportedAccess("Velo 1 IT8368E ReadDword", a, 0); }
    void WriteByte (uint32_t a, uint8_t  v) override { HaltUnsupportedAccess("Velo 1 IT8368E WriteByte", a, v); }
    void WriteWord (uint32_t a, uint32_t v) override { HaltUnsupportedAccess("Velo 1 IT8368E WriteWord", a, v); }
    void WriteDword(uint32_t a, uint64_t v) override { HaltUnsupportedAccess("Velo 1 IT8368E WriteDword", a, v); }

    void SaveState(StateWriter& w) override { emu_.Get<IteIt8368>().SaveState(w); }
    void RestoreState(StateReader& r) override { emu_.Get<IteIt8368>().RestoreState(r); }
};

}  /* namespace */

REGISTER_SERVICE(PhilipsVelo1SocketController);
