#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/service.h"

namespace {

class Imx6Ccm : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        /* CCM hardware reset values, IMX6SDLRM Rev.1 §18.7 Table 18-5..18-23.
           Critical: divider POST fields must be non-zero where the CE OAL
           clock-tree dumper asserts (cbnz/UDF #0xF9 at nk.exe+0xB9A2). */
        regs_[0x00u >> 2] = 0x040116FFu; /* CCR  */
        regs_[0x04u >> 2] = 0x00000000u; /* CCDR */
        regs_[0x08u >> 2] = 0x00000010u; /* CSR  */
        regs_[0x0Cu >> 2] = 0x00000100u; /* CCSR */
        regs_[0x10u >> 2] = 0x00000000u; /* CACRR */
        regs_[0x14u >> 2] = 0x00018D40u; /* CBCDR — periph2 from PLL2, ARM/AHB/IPG divs */
        regs_[0x18u >> 2] = 0x00022324u; /* CBCMR — POR (IMX6SDLRM/QEMU);
                                            bit13 gpu3d_core_clk_sel was dropped */
        regs_[0x1Cu >> 2] = 0x00F00000u; /* CSCMR1 */
        regs_[0x20u >> 2] = 0x02B92F06u; /* CSCMR2 */
        regs_[0x24u >> 2] = 0x00490B00u; /* CSCDR1 - uSDHC PODF, UART PODF=0 (div 1) */
        regs_[0x28u >> 2] = 0x0EC102C1u; /* CS1CDR */
        regs_[0x2Cu >> 2] = 0x000736C1u; /* CS2CDR */
        regs_[0x30u >> 2] = 0x33F71F92u; /* CDCDR */
        regs_[0x34u >> 2] = 0x0002A150u; /* CHSCCDR */
        regs_[0x38u >> 2] = 0x0002A150u; /* CSCDR2 */
        regs_[0x3Cu >> 2] = 0x00014841u; /* CSCDR3 */
        regs_[0x40u >> 2] = 0x00000000u; /* CSCDR4 */
        regs_[0x44u >> 2] = 0x00000000u; /* CWDR */
        regs_[0x48u >> 2] = 0x00000000u; /* CDHIPR — idle */
        regs_[0x4Cu >> 2] = 0x00000000u;
        regs_[0x50u >> 2] = 0x00000000u;
        regs_[0x54u >> 2] = 0x00000079u; /* CLPCR */
        regs_[0x58u >> 2] = 0x00000000u; /* CISR */
        regs_[0x5Cu >> 2] = 0xFFFFFFFFu; /* CIMR */
        regs_[0x60u >> 2] = 0x000A0001u; /* CCOSR */
        regs_[0x64u >> 2] = 0x0000FE62u; /* CGPR */
        /* CCGR0..CCGR6 (0x68..0x80): all IP clocks running, default for CE init. */
        for (uint32_t off = 0x68u; off <= 0x80u; off += 4u)
            regs_[off >> 2] = 0xFFFFFFFFu;
        regs_[0x84u >> 2] = 0x00000000u; /* CMEOR */
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }
    uint32_t MmioBase() const override { return 0x020C4000u; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x48u) return 0u; /* CDHIPR: no divider handshake busy */
        if (off <= 0x8Cu && (off & 3u) == 0) return regs_[off >> 2];
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x48u) return;
        if (off <= 0x8Cu && (off & 3u) == 0) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }
    uint32_t regs_[0x90u / 4u]{};
};

} /* namespace */

REGISTER_SERVICE(Imx6Ccm);
