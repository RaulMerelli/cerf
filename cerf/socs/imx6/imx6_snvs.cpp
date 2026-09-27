#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/virtual_clock.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

#include <mutex>
#include "imx6_id.h"

namespace {

/* Linux rtc-snvs.c defines LP base +0x34 and the 47-bit counter shifted by 15;
   IMX6DQ6SDLSRM Rev.D sections 6.10.20-6.10.26 define its registers. */
class Imx6Snvs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
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
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x38u: return lpcr_;
        case 0x4Cu: return lpsr_;
        case 0x50u: return static_cast<uint32_t>(RtcCounterLocked() >> 32);
        case 0x54u: return static_cast<uint32_t>(RtcCounterLocked());
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x38u:
            if ((value & ~kSrtcEnable) != 0u)
                HaltUnsupportedAccess("imx6-snvs LPCR alarm, wake and power-off control", addr, value);
            RebaseCounterLocked();
            lpcr_ = value;
            return;
        case 0x4Cu:  lpsr_ &= ~value; return;
        case 0x50u:
            if ((lpcr_ & kSrtcEnable) == 0)
                rtc_base_ = (rtc_base_ & 0xFFFFFFFFu) | (static_cast<uint64_t>(value & 0x7FFFu) << 32);
            return;
        case 0x54u:
            if ((lpcr_ & kSrtcEnable) == 0) rtc_base_ = (rtc_base_ & 0x7FFF00000000ull) | value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lock(mtx_);
        w.Write("rtc_counter", RtcCounterLocked());
        w.Write("lpcr", lpcr_);
        w.Write("lpsr", lpsr_);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lock(mtx_);
        r.Read("rtc_counter", rtc_base_);
        rtc_baseline_ns_ = NowNs();
        r.Read("lpcr", lpcr_);
        r.Read("lpsr", lpsr_);
    }

private:
    /* IMX6DQ6SDLSRM Rev. D §6.10.15: LPCR SRTC_ENV[0] enables the secure real time counter;
       LPTA_EN[1] arms the time alarm and TOP[6] signals the PMIC to turn the system off. */
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

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == 0x4Cu) {
            std::lock_guard<std::mutex> lock(mtx_);
            lpsr_ &= ~lane.value;
            return;
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    mutable std::mutex mtx_;
    uint32_t lpcr_ = 0;
    /* IMX6DQ6SDLSRM Rev. D §6.10.20: LPSR resets with PGD[3] set. */
    uint32_t lpsr_ = 0x00000008u;
    uint64_t rtc_base_ = 0;
    int64_t rtc_baseline_ns_ = 0;
};

}

REGISTER_SERVICE(Imx6Snvs);
