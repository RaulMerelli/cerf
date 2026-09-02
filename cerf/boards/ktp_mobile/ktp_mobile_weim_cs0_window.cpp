#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cstdint>

namespace {

/* IMX6SDLRM Rev.4 Table 2-1 System memory map (p. 211): PA 0800_0000 through
   0FFF_FFFF is the EIM (NOR/SRAM) external-memory aperture, 128 MB.  On this
   BSP late code reaches PA 0x08000000 directly after ENET init.  The KTP400
   wires no chip select there, so the bus floats: unwired reads return open
   bus, and writes are accepted into a small shadow so a probe that writes
   then reads back sees its own value. */
class KtpMobileWeimCs0Window : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return 0x08000000u; }
    uint32_t MmioSize() const override { return 0x08000000u; } /* 128 MB */

    uint8_t ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
    void WriteByte(uint32_t addr, uint8_t value) override { Store(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Store(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override { Store(addr, value, 4); }

private:
    void Store(uint32_t addr, uint32_t value, uint32_t bytes) {
        const uint32_t off = addr - MmioBase();
        for (uint32_t i = 0; i < bytes; ++i)
            shadow_[(off + i) & 0xFFu] = static_cast<uint8_t>(value >> (i * 8u));
    }

    uint8_t shadow_[0x100] = {};
};

} /* namespace */

REGISTER_SERVICE(KtpMobileWeimCs0Window);
