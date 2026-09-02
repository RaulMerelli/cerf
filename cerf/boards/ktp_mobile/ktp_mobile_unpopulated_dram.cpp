#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cstdint>

namespace {

/* IMX6SDLRM Rev.4 Table 2-1 System memory map (p. 211): PA 1000_0000 through
   FFFF_FFFF is the MMDC DDR controller aperture.  The KTP400 populates
   384 MB of it, so PA 1000_0000..27FF_FFFF is backed and everything above is
   DDR address space with no device behind it.

   The CE kernel probes that space by temporarily mapping candidate physical
   pages at VA 0x9D4A0000.  An unpopulated DDR bus floats, so reads return
   open bus and writes land nowhere; modelling that keeps the probe from
   reaching the generic unbacked-PA path, which would read it as a missing
   MMIO peripheral. */
class KtpMobileUnpopulatedDram : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    /* 2800_0000..7FFF_FFFF: the whole unpopulated span up to the top of the
       address range the OAL ever walks. */
    uint32_t MmioBase() const override { return 0x28000000u; }
    uint32_t MmioSize() const override { return 0x58000000u; }

    uint8_t ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
    void WriteByte(uint32_t, uint8_t) override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

} /* namespace */

REGISTER_SERVICE(KtpMobileUnpopulatedDram);
