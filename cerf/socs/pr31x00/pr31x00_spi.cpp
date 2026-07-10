#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "pr31x00_spi_slave.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x10C00160u;

constexpr uint32_t kOffCtl    = 0x00u;   /* $160 */
constexpr uint32_t kOffData   = 0x04u;   /* $164 TXDATA (W) / RXDATA (R) */

/* SPI Control (§14.3.1): SPION<17>(R) EMPTY<16>(R) DELAYVAL[3:0]<15:12>
   BAUDRATE[3:0]<11:8> PHAPOL<5> CLKPOL<4> WORD<2> LSB<1> ENSPI<0>; bits 31-18,
   7-6 and 3 reserved. */
constexpr uint32_t kCtlWritable = 0x0000FF37u;
constexpr uint32_t kCtlRsvdOrRo = 0xFFFF00C8u;
constexpr uint32_t kCtlSpion    = 0x00020000u;
constexpr uint32_t kCtlEmpty    = 0x00010000u;
constexpr uint32_t kCtlEnSpi    = 0x00000001u;

class Pr31x00Spi : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::PR31500 || soc == SocFamily::PR31700;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x8u; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kBase) {
            case kOffCtl:
                /* SPION and EMPTY report the Transmitter Holding and Shift Registers,
                   which are empty while no transfer is staged (§14.3.1). */
                return ctl_ | kCtlEmpty | ((ctl_ & kCtlEnSpi) ? kCtlSpion : 0u);

            /* RXDATA is only valid after SPIRCVINT (§14.3.3), which the slave asserts
               while it has a byte staged. */
            case kOffData: {
                auto* slave = emu_.TryGet<Pr31x00SpiSlave>();
                if (slave && slave->SpiRxHasByte()) return slave->SpiRxReadByte();
                HaltUnsupportedAccess("PR31x00 SPI RXDATA read with no byte staged", addr, 0);
            }

            default:
                HaltUnsupportedAccess("PR31x00 SPI ReadWord", addr, 0);
        }
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - kBase) {
            case kOffCtl:
                if (value & kCtlRsvdOrRo) {
                    HaltUnsupportedAccess("PR31x00 SPI CTL reserved or read-only bit", addr, value);
                }
                /* DELAYVAL and BAUDRATE divide SPICLK and space characters on it
                   (§14.3.1). */
                ctl_ = value & kCtlWritable;
                return;

            /* A TXDATA write loads the Transmitter Holding Register and clocks the
               character out to the slave devices (§14.3.2). */
            case kOffData:
                HaltUnsupportedAccess("PR31x00 SPI TXDATA write with no slave on the bus", addr, value);

            default:
                HaltUnsupportedAccess("PR31x00 SPI WriteWord", addr, value);
        }
    }

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 SPI ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 SPI ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 SPI WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 SPI WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(ctl_); }
    void RestoreState(StateReader& r) override { r.Read(ctl_); }

private:
    /* Every writable field resets to 0 (§14.3.1); DELAYVAL and BAUDRATE reset
       undefined and are taken as 0. */
    uint32_t ctl_ = 0x00000000u;
};

}  /* namespace */

REGISTER_SERVICE(Pr31x00Spi);
