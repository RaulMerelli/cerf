#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* Secure Non-Volatile Storage block.

   i.MX6 exposes the low-power RTC at SNVS + 0x34 (Linux rtc-snvs.c uses
   SNVS_LPREGISTER_OFFSET=0x34).  The counter is a 47-bit 32.768 kHz value;
   seconds are stored/read by shifting by 15.  KTP400's early OAL currently
   only touches LPCR in our traces; the counter is still modeled for drivers
   that use the SNVS RTC path later. */
class Imx6Snvs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020CC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(
            ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(
            ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x38u: return lpcr_;                  /* LPCR */
        case 0x4Cu: return lpsr_;                  /* LPSR */
        case 0x50u: return static_cast<uint32_t>(RtcCounter() >> 32); /* LPSRTCMR */
        case 0x54u: return static_cast<uint32_t>(RtcCounter());       /* LPSRTCLR */
        case 0x58u: return lptar_;                 /* LPTAR */
        case 0x64u: return lppgdr_;                /* LPPGDR */
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
        switch (off) {
        case 0x38u:                                /* LPCR */
            lpcr_ = value;
            return;
        case 0x4Cu:                                /* LPSR: write-1-to-clear */
            lpsr_ &= ~value;
            return;
        case 0x50u:                                /* LPSRTCMR */
            rtc_msb_ = value & 0x7FFFu;
            rtc_read_ticks_ = 0;
            return;
        case 0x54u:                                /* LPSRTCLR */
            rtc_lsb_ = value;
            rtc_read_ticks_ = 0;
            return;
        case 0x58u:                                /* LPTAR */
            lptar_ = value;
            return;
        case 0x64u:                                /* LPPGDR */
            lppgdr_ = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

private:
    uint64_t RtcCounter() {
        /* Advance a little on every MMIO read.  This is deliberately slow
           enough that the Linux-style double-read consistency check stays
           happy, while write-sync loops still observe forward progress. */
        return ((static_cast<uint64_t>(rtc_msb_) << 32) | rtc_lsb_) +
               (rtc_read_ticks_++ & 0xFFFFu);
    }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask =
            (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t lpcr_ = 0x00000001u;       /* SRTC_ENV */
    uint32_t lpsr_ = 0;
    uint32_t lptar_ = 0;
    uint32_t lppgdr_ = 0x41736166u;     /* Linux SNVS_LPPGDR_INIT */
    uint32_t rtc_msb_ = (1782648000ull << 15) >> 32; /* 2026-06-28 12:00 UTC */
    uint32_t rtc_lsb_ = static_cast<uint32_t>(1782648000ull << 15);
    uint32_t rtc_read_ticks_ = 0;
};

/* WDOG registers are 16-bit (ReadHalf). WRSR bit 4 = POR; clearing sends OAL
   to warm-boot path and boot stalls. */
class Imx6Wdog1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020BC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadHalf(addr & ~1u) >> ((addr & 1u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        return ReadHalf(addr) | (static_cast<uint32_t>(ReadHalf(addr + 2u)) << 16);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - MmioBase()) {
        case 0x00u: return wcr_;
        case 0x02u: return wsr_;
        case 0x04u: return 0x0010u;   /* WRSR: POR (read-only). */
        case 0x06u: return wicr_;
        case 0x08u: return wmcr_;
        }
        HaltUnsupportedAccess("read16", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t aligned = addr & ~1u;
        const uint32_t shift = (addr & 1u) * 8u;
        WriteHalf(aligned,
            static_cast<uint16_t>((ReadHalf(aligned) & ~(0xFFu << shift)) |
                                  ((value & 0xFFu) << shift)));
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        WriteHalf(addr, static_cast<uint16_t>(value));
        WriteHalf(addr + 2u, static_cast<uint16_t>(value >> 16));
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - MmioBase()) {
        case 0x00u: wcr_  = value; return;   /* WDE/WT â€” no timeout modeled. */
        case 0x02u: wsr_  = value; return;   /* service sequence 0x5555/0xAAAA. */
        case 0x04u:                return;   /* WRSR is read-only. */
        case 0x06u: wicr_ = value; return;
        case 0x08u: wmcr_ = value; return;
        }
        HaltUnsupportedAccess("write16", addr, value);
    }

private:
    uint16_t wcr_  = 0x0030u;   /* WCR reset, IMX6SDLRM Tab 73-4. */
    uint16_t wsr_  = 0x0000u;
    uint16_t wicr_ = 0x0004u;
    uint16_t wmcr_ = 0x0001u;   /* WMCR.PDE power-down enable set at reset. */
};

class Imx6Wdog2 : public Imx6Wdog1 {
public:
    using Imx6Wdog1::Imx6Wdog1;
    uint32_t MmioBase() const override { return 0x020C0000u; }
};

/* SCR warm-reset and per-core bits self-clear in HW; clearing on write is mandatory or OAL reset-complete poll stalls. */
class Imx6Src : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        regs_[0x00u >> 2] = 0x00000521u;  /* SCR reset, IMX6SDLRM Tab 65-3. */
        regs_[0x08u >> 2] = 0x00000001u;  /* SRSR: POR (cold boot). */
        regs_[0x18u >> 2] = 0x0000001Fu;  /* SIMR: all reset sources masked. */
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020D8000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off <= 0x44u && (off & 3u) == 0) return regs_[off >> 2];
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
        if (off == 0x00u) {
            /* SCR: clear the one-shot core/warm-reset request bits so the
               OAL's "reset issued" poll sees them self-clear (RM Â§65.7.1:
               *_RST and warm_reset_enable are W1S, hardware-cleared). */
            regs_[0] = value & ~0x0000E00Eu;
            return;
        }
        if (off == 0x08u) {                /* SRSR: write-1-to-clear. */
            regs_[0x08u >> 2] &= ~value;
            return;
        }
        if (off <= 0x44u && (off & 3u) == 0) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t regs_[0x48u / 4u]{};
};

}  /* namespace */

REGISTER_SERVICE(Imx6Snvs);
REGISTER_SERVICE(Imx6Wdog1);
REGISTER_SERVICE(Imx6Wdog2);
REGISTER_SERVICE(Imx6Src);
