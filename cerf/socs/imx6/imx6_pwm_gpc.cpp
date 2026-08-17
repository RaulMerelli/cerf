#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* i.MX27-and-later PWM block.

   Register layout and status semantics follow Linux drivers/pwm/pwm-imx27.c:
     PWMCR  0x00, PWMSR 0x04, PWMIR 0x08, PWMSAR 0x0c, PWMPR 0x10, PWMCNR 0x14.

   KTP400 uses an i.MX6 Solo/DL-class SoC; its PWM instances are the same
   MX3/MX27-style controller.  The important hardware behaviours for boot are:
   - PWMCR.SWR self-clears after software reset.
   - PWMSR.FIFOAV reports available sample FIFO slots; an empty/available FIFO
     prevents backlight/display drivers from waiting forever before writing SAR.
   - PWMCNR advances while enabled enough for software sanity checks. */
template <uint32_t kBase>
class Imx6Pwm : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        regs_[kOffSr >> 2] = kSrFifoAv4Words;
        regs_[kOffPr >> 2] = 0x0000FFFFu;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case kOffCr:
            return regs_[kOffCr >> 2] & ~kCrSwr;
        case kOffSr:
            return (regs_[kOffSr >> 2] & ~kSrFifoAvMask) | kSrFifoAv4Words;
        case kOffCnr:
            return Counter();
        case kOffIr:
        case kOffSar:
        case kOffPr:
            return regs_[off >> 2];
        default:
            HaltUnsupportedAccess("read32", addr, 0);
        }
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case kOffCr:
            regs_[kOffCr >> 2] = value & ~kCrSwr;   /* software reset completes immediately */
            if (value & kCrSwr) {
                regs_[kOffSr >> 2] = kSrFifoAv4Words;
                regs_[kOffSar >> 2] = 0;
                counter_ = 0;
            }
            return;
        case kOffSr:
            regs_[kOffSr >> 2] &= ~value;           /* status bits are W1C */
            regs_[kOffSr >> 2] |= kSrFifoAv4Words;
            return;
        case kOffIr:
        case kOffSar:
        case kOffPr:
            regs_[off >> 2] = value;
            if (off == kOffSar) regs_[kOffSr >> 2] = kSrFifoAv4Words;
            return;
        case kOffCnr:
            counter_ = value;
            return;
        default:
            HaltUnsupportedAccess("write32", addr, value);
        }
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(regs_, sizeof(regs_));
        w.Write(counter_);
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_, sizeof(regs_));
        r.Read(counter_);
    }

private:
    static constexpr uint32_t kOffCr  = 0x00u;
    static constexpr uint32_t kOffSr  = 0x04u;
    static constexpr uint32_t kOffIr  = 0x08u;
    static constexpr uint32_t kOffSar = 0x0Cu;
    static constexpr uint32_t kOffPr  = 0x10u;
    static constexpr uint32_t kOffCnr = 0x14u;

    static constexpr uint32_t kCrEn  = 1u << 0;
    static constexpr uint32_t kCrSwr = 1u << 3;

    static constexpr uint32_t kSrFifoAvMask   = 0x7u;
    static constexpr uint32_t kSrFifoAv4Words = 0x4u;

    uint32_t Counter() {
        if (regs_[kOffCr >> 2] & kCrEn) {
            const uint32_t period = regs_[kOffPr >> 2] ? regs_[kOffPr >> 2] : 1u;
            counter_ = (counter_ + 1u) % (period + 2u);
        }
        return counter_;
    }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }


    uint32_t regs_[0x18u / 4u]{};
    uint32_t counter_ = 0;
};

/* General Power Controller / PGC.

   This is a retained register model for the i.MX6 GPC aperture.  Linux and
   vendor BSPs program GPC_CNTR, interrupt-mask registers and PGC power-up/down
   handshakes while enabling GPU/display domains.  Hardware eventually clears
   request bits and reports domains powered; model that instead of open-bus.
 */
class Imx6Gpc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        regs_[0x000u >> 2] = 0x00000000u;   /* GPC_CNTR */
        regs_[0x004u >> 2] = 0x00000000u;   /* GPC_PGR */
        regs_[0x008u >> 2] = 0x00000000u;   /* GPC_IMR1 */
        regs_[0x00Cu >> 2] = 0x00000000u;   /* GPC_IMR2 */
        regs_[0x010u >> 2] = 0x00000000u;   /* GPC_IMR3 */
        regs_[0x014u >> 2] = 0x00000000u;   /* GPC_IMR4 */
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020DC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
            if (off == 0x000u) return regs_[0] & ~0x3u;  /* power up/down requests self-clear */
            return regs_[off >> 2];
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
            regs_[off >> 2] = (off == 0x000u) ? (value & ~0x3u) : value;
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

    uint32_t regs_[0x4000u / 4u]{};
};

class Imx6Pwm1 : public Imx6Pwm<0x02080000u> { using Imx6Pwm::Imx6Pwm; };
class Imx6Pwm2 : public Imx6Pwm<0x02084000u> { using Imx6Pwm::Imx6Pwm; };
class Imx6Pwm3 : public Imx6Pwm<0x02088000u> { using Imx6Pwm::Imx6Pwm; };
class Imx6Pwm4 : public Imx6Pwm<0x0208C000u> { using Imx6Pwm::Imx6Pwm; };

}  /* namespace */

REGISTER_SERVICE(Imx6Pwm1);
REGISTER_SERVICE(Imx6Pwm2);
REGISTER_SERVICE(Imx6Pwm3);
REGISTER_SERVICE(Imx6Pwm4);
REGISTER_SERVICE(Imx6Gpc);
