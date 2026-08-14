#include "../vr41xx/vr41xx_pmu_impl.h"

#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

using cerf_vr41xx_pmu_detail::Vr41xxPmuBase;
using cerf_vr41xx_pmu_detail::Vr41xxPmuModel;

/* VR4102 PMU (Power Management Unit), Internal I/O Space 2, 0x0B0000A0-0x0B0000BF
   (UM Table 15-4): PMUINTREG@0x00, PMUCNTREG@0x02, PMUINT2REG@0x04,
   PMUCNT2REG@0x06, 16-bit. */
constexpr Vr41xxPmuModel kModel = {
    /*base=*/0x0B0000A0u,
    /*size=*/0x20u,
    /* PMUINTREG bit classes (UM p328-329). Cause bits are write-1-to-clear; the two
       lock bits are plain software R/W (never set by hardware); D10 DCDST is a
       read-only pin state and D11 is reserved (both stay 0 - no source drives them). */
    /*int_w1c=*/0xF33Fu,        /* D15-12 GPIOxINTR, D9 RTCINTR, D8 BATTINH, D5-0 *RST/*INTR */
    /*int_sw_rw=*/0x00C0u,      /* D7 BATTLOCK, D6 CARDLOCK */
    /* Cold power-on is the RTC-domain reset (UM Table 15-1 + p328 reset column), and
       guest nk.exe start() (0x9F001CA4: read PMUINTREG, isolate D4, branch) requires
       D4 set to take the cold-boot path. */
    /*int_power_on=*/0x0010u,   /* RTCRST: D4 RTCRST */
    /* PMUCNTREG (UM p331): D15-8 GPIO3-0 MSK/TRG + D7 STANDBY + D2 HALTIMERRST R/W;
       D6-3/D0 reserved "write 0 ... 0 is returned after a read", D1 reserved "write 1
       ... 1 is returned". HALTIMERRST is stored with NO HAL-timer modeled behind it;
       past its ~4 s expiry "an automatic shutdown is performed". */
    /*cnt_writable=*/0xFF84u,
    /*cnt_fixed_read=*/0x0002u,
    /*cnt_power_on=*/0x8802u,   /* RTCRST: GPIO3MSK(D15) + GPIO3TRG(D11) + D1 */
    0u,
    0u,
    0x0008u,   /* D3 RSTSW  (reset switch / soft reset) */
    0x0010u,   /* D4 RTCRST (RTC-domain / cold reset)   */
};

constexpr uint16_t kIntDmSrst   = 0x0004u;  /* D2 DMSRST  (deadman's-switch reset)    */
constexpr uint16_t kIntPowerSw  = 0x0001u;  /* D0 POWERSW (power-switch interrupt)    */

class Vr4102Pmu : public Vr41xxPmuBase<SocFamily::VR4102, kModel> {
public:
    using Vr41xxPmuBase::Vr41xxPmuBase;

    /* PMUINTREG latches the reset cause (UM 15.1.1): start() cold-JUMPOUTs to
       0xBF0043C8 iff RTCRST(D4)/RSTSW(D3) set, else resumes a stale save block and
       hangs. */
    void LatchWatchdogReset() override { SetIntBits(kIntDmSrst); }

    /* "Recovery from reset status occurs when the POWER pin is asserted" (UM 7.1.4), and
       PMUINTREG D0 POWERSWINTR is "POWER switch interrupt detection. Cleared to 0 when 1 is
       written" with no Hibernate exclusion (UM 15.2.1, p329). */
    void LatchSleepWakeCause() override { SetIntBits(kIntPowerSw); }
    void ClearSleepWakeCause() override { ClearIntBits(kIntPowerSw); }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Pmu);
