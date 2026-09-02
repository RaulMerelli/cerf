#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/virtual_clock.h"
#include "../../state/state_stream.h"

#include <mutex>

namespace {

/* Secure Non-Volatile Storage block.

   i.MX6 exposes the low-power RTC at SNVS + 0x34 (Linux rtc-snvs.c uses
   SNVS_LPREGISTER_OFFSET=0x34).  The counter is a 47-bit 32.768 kHz value;
   seconds are stored/read by shifting by 15.  Register reset values and the
   SRTC_ENV/write-disable rules follow IMX6DQ6SDLSRM Rev. D, sections 6.10.15
   and 6.10.20-6.10.26. */
class Imx6Snvs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            rtc_baseline_ns_ = NowNs();
        }
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020CC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x38u: return lpcr_;                                           /* LPCR */
        case 0x4Cu: return lpsr_;                                           /* LPSR */
        case 0x50u: return static_cast<uint32_t>(RtcCounterLocked() >> 32); /* LPSRTCMR */
        case 0x54u: return static_cast<uint32_t>(RtcCounterLocked());       /* LPSRTCLR */
        case 0x58u: return lptar_;                                          /* LPTAR */
        case 0x64u: return lppgdr_;                                         /* LPPGDR */
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x38u: /* LPCR */
            RebaseCounterLocked();
            lpcr_ = value;
            return;
        case 0x4Cu: /* LPSR: write-1-to-clear */ lpsr_ &= ~value; return;
        case 0x50u: /* LPSRTCMR */
            if ((lpcr_ & kSrtcEnable) == 0)
                rtc_base_ = (rtc_base_ & 0xFFFFFFFFu) | (static_cast<uint64_t>(value & 0x7FFFu) << 32);
            return;
        case 0x54u: /* LPSRTCLR */
            if ((lpcr_ & kSrtcEnable) == 0) rtc_base_ = (rtc_base_ & 0x7FFF00000000ull) | value;
            return;
        case 0x58u: /* LPTAR */ lptar_ = value; return;
        case 0x64u: /* LPPGDR */ lppgdr_ = value; return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lock(mtx_);
        w.Write(RtcCounterLocked());
        w.Write(lpcr_);
        w.Write(lpsr_);
        w.Write(lptar_);
        w.Write(lppgdr_);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lock(mtx_);
        r.Read(rtc_base_);
        rtc_baseline_ns_ = NowNs();
        r.Read(lpcr_);
        r.Read(lpsr_);
        r.Read(lptar_);
        r.Read(lppgdr_);
    }

private:
    /* IMX6DQ6SDLSRM Rev.D section 6.10.15 (p. 372): LPCR bit 0 SRTC_ENV,
       "Secure Real Time Counter Enable and Valid. When set, the SRTC becomes
       operational."  IMX6SDLRM section 57.9.10 redacts this bit as a
       "Security-related field". */
    static constexpr uint32_t kSrtcEnable = 1u;
    static constexpr uint64_t kCounterMask = 0x7FFFFFFFFFFFull;
    static constexpr uint64_t kCyclesPerSecond = 32768u;
    static constexpr uint64_t kNanosecondsPerSecond = 1000000000u;

    uint64_t RtcCounterLocked() const {
        if ((lpcr_ & kSrtcEnable) == 0) return rtc_base_;

        const int64_t elapsed = NowNs() - rtc_baseline_ns_;
        if (elapsed <= 0) return rtc_base_ & kCounterMask;
        const uint64_t ns = static_cast<uint64_t>(elapsed);
        const uint64_t cycles = (ns / kNanosecondsPerSecond) * kCyclesPerSecond +
                                ((ns % kNanosecondsPerSecond) * kCyclesPerSecond) / kNanosecondsPerSecond;
        return (rtc_base_ + cycles) & kCounterMask;
    }

    void RebaseCounterLocked() {
        rtc_base_ = RtcCounterLocked();
        rtc_baseline_ns_ = NowNs();
    }

    int64_t NowNs() const { return emu_.Get<VirtualClock>().NowNs(); }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    mutable std::mutex mtx_;
    uint32_t lpcr_ = 0;
    uint32_t lpsr_ = 0x00000008u;
    uint32_t lptar_ = 0;
    uint32_t lppgdr_ = 0;
    uint64_t rtc_base_ = 0;
    int64_t rtc_baseline_ns_ = 0;
};

} /* namespace */

REGISTER_SERVICE(Imx6Snvs);
