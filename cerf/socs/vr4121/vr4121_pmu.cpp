#include "../vr41xx_pmu_impl.h"

#include "../../core/cerf_emulator.h"
#include "../guest_cpu_reset.h"

#include <atomic>
#include <cstdint>

namespace {

using cerf_vr41xx_pmu_detail::Vr41xxPmuBase;
using cerf_vr41xx_pmu_detail::Vr41xxPmuModel;

/* VR4121 PMU, Internal I/O Space 2 (UM Table 1-6): PMUINTREG@0x00, PMUCNTREG@0x02,
   PMUINT2REG@0x04, PMUCNT2REG@0x06, PMUWAITREG@0x08, PMUDIVREG@0x0C, 16-bit. The RTC
   block follows at 0x0B0000C0 (UM Table 1-7), so the PMU decodes 0x0B0000A0-BF. */
constexpr Vr41xxPmuModel kModel = {
    /*base=*/0x0B0000A0u,
    /*size=*/0x20u,
    /* PMUINTREG (UM 16.2.1), "Cleared to 0 when 1 is written": D15-12 GPIOxINTR,
       D9 RTCINTR, D8 BATTINH, D5 TIMOUTRST, D4 RTCRST, D3 RSTSW, D2 DMSRST,
       D1 BATTINTR, D0 POWERSWINTR. D11 RFU reads 0; D10 DCDST is the DCD# pin. */
    /*int_w1c=*/0xF33Fu,
    /*int_sw_rw=*/0x00C0u,     /* D7:6 memo(1:0), "can be used by users freely" */
    /*int_power_on=*/0x0010u,  /* RTCRST column: RTCRST(D4) = 1, every other bit 0 */
    /* PMUCNTREG (UM 16.2.2): D15-12 GPIO(3:0)MSK, D11-8 GPIO(3:0)TRG, D7 STANDBY and
       D2 HALTIMERRST are R/W; D6:3 and D0 are RFU reading 0.
       HALTIMERRST (D2) is stored R/W with no HAL-timer behind it: the HALTimer is a
       one-shot activation watchdog - "the software must write '1' to PMUCNTREG's
       HALTIMERRST bit within about four seconds" after activation (UM 16.1.2(1)), and
       its expiry is an automatic shutdown, not a reboot (UM 8.1.5) - so a guest that
       completes its activation sequence disarms it once and forever. */
    /*cnt_writable=*/0xFF84u,
    /*cnt_fixed_read=*/0x0002u,  /* D1 RFU: "Write 1 to this bit. 1 is returned after a read." */
    /*cnt_power_on=*/0x8802u,    /* RTCRST column: "The GPIO3MSK bit is set to 1 by RTCRST,
                                    and the other bits are cleared to 0" - D15 + D11 + D1. */
};

constexpr uint32_t kOffWaitReg = 0x08u;

constexpr uint16_t kIntRtcRst = 0x0010u;   /* PMUINTREG D4 RTCRST */
constexpr uint16_t kIntRstSw = 0x0008u;   /* PMUINTREG D3 RSTSW  */

/* PMUWAITREG (UM 16.2.5): D13:0 WCOUNT, "Activation wait time = WCOUNT x (1/32.768)
   ms"; D15:14 RFU. "This register is set to 0x2C00 (it sets 343.75-ms activation wait
   time) after RTC reset", and its After-reset row holds the value before reset. */
constexpr uint16_t kWaitWcount  = 0x3FFFu;
constexpr uint16_t kWaitPowerOn = 0x2C00u;

class Vr4121Pmu : public Vr41xxPmuBase<SocFamily::VR4121, kModel> {
public:
    using Vr41xxPmuBase::Vr41xxPmuBase;

    /* "When the RTCRST# signal becomes active, the PMU resets all peripheral units
       including the RTC unit" (UM 16.1.1(1)): every PMU register takes its RTCRST column.
       An RSTSW reset (UM 16.1.1(2)) and every shutdown (UM 16.1.2, Table 16-2) reset "all
       peripheral units except for RTC and PMU", so the PMU keeps its registers and only
       the new cause is OR'd into PMUINTREG. */
    void OnReady() override {
        Vr41xxPmuBase::OnReady();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind != ResetLineKind::Rtc) return;
            StoreIntReg(kModel.int_power_on);
            cntreg_  = kModel.cnt_power_on;
            waitreg_ = kWaitPowerOn;
        });
    }

    void LatchWarmReset() override { SetIntBits(kIntRstSw); }
    void LatchColdReset() override { SetIntBits(kIntRtcRst); }

    /* A deadman's SW shutdown sets DMSRST and RSTSW (UM 16.2.1, 16.1.2(2)), but CERF
       raises a watchdog reset from no VR4121 peripheral: the DSU (UM ch.18) is not
       implemented and the HALTimer has no dog (see PMUCNTREG D2 above). */
    void LatchWatchdogReset() override {
        HaltUnsupportedAccess("VR4121 PMU watchdog reset cause", kModel.base, IntReg());
    }

    /* Software shutdown's PMUINTREG column is "-" (UM Table 16-2), and D0 POWERSWINTR "is
       not set to 1 when the POWER signal becomes high in the Hibernate mode (MPOWER = 0)"
       (UM 16.2.1). */
    void LatchSleepWakeCause() override {}
    void ClearSleepWakeCause() override {}

    void SaveState(StateWriter& w) override {
        Vr41xxPmuBase::SaveState(w);
        w.Write(waitreg_);
    }

    void RestoreState(StateReader& r) override {
        Vr41xxPmuBase::RestoreState(r);
        r.Read(waitreg_);
    }

protected:
    void WriteHalfExt(uint32_t addr, uint16_t value) override {
        if (addr - kModel.base == kOffWaitReg) {
            waitreg_ = static_cast<uint16_t>(value & kWaitWcount);
            return;
        }
        Vr41xxPmuBase::WriteHalfExt(addr, value);
    }

private:
    uint16_t waitreg_ = kWaitPowerOn;
};

}  /* namespace */

REGISTER_SERVICE(Vr4121Pmu);
