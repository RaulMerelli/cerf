#pragma once

#include "../peripherals/peripheral_base.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../peripherals/peripheral_dispatcher.h"

#include <cstdint>

/* Shared 16-bit byte/half/word framing for the Freescale WDOG. The register set
   is per-SoC (i.MX31 RM Ch 37: WCR/WSR/WRSR; i.MX51 RM Ch 62 adds WICR/WMCR), so
   each concrete owns ReadReg16/WriteReg16 + SaveState, and an offset a given SoC
   does not define halts as unimplemented MMIO. */
namespace cerf_freescale_wdog_detail {

constexpr uint32_t kSize = 0x00004000u;  /* AIPS 16 KB peripheral slot */

constexpr uint32_t kWcr  = 0x00u;  /* Watchdog Control Register   */
constexpr uint32_t kWsr  = 0x02u;  /* Watchdog Service Register   */
constexpr uint32_t kWrsr = 0x04u;  /* Watchdog Reset Status (R/O) */
constexpr uint32_t kWicr = 0x06u;  /* Watchdog Interrupt Control (i.MX51) */
constexpr uint32_t kWmcr = 0x08u;  /* Watchdog Misc. Control (i.MX51)     */

constexpr uint16_t kWcrReset = 0x0030u;  /* WDA(5)|SRS(4) - i.MX51 Table 62-5 / i.MX31 Table 37-3 */

template <uint32_t Base, SocFamily Soc>
class FreescaleWdogBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == Soc;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return Base; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint16_t v = ReadReg16((addr - Base) & ~1u);
        return ((addr & 1u) ? (v >> 8) : v) & 0xFFu;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = (addr - Base) & ~1u;
        uint16_t v = ReadReg16(off);
        v = (addr & 1u) ? ((v & 0x00FFu) | (uint16_t(value) << 8))
                        : ((v & 0xFF00u) | value);
        WriteReg16(off, v);
    }

    uint16_t ReadHalf(uint32_t addr) override { return ReadReg16(addr - Base); }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        WriteReg16(addr - Base, value);
    }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - Base;
        return ReadReg16(off) | (uint32_t(ReadReg16(off + 2)) << 16);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - Base;
        WriteReg16(off, value & 0xFFFFu);
        WriteReg16(off + 2, value >> 16);
    }

protected:
    virtual uint16_t ReadReg16(uint32_t off)                 = 0;
    virtual void     WriteReg16(uint32_t off, uint16_t value) = 0;
};

}  /* namespace cerf_freescale_wdog_detail */
