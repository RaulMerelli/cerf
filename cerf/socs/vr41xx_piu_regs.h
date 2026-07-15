#pragma once

#include "../boards/board_context.h"

#include <cstdint>

namespace cerf_vr41xx_piu_detail {

/* VR4121 UM Table 20-1 == VR4102 UM Table 19-1. */
constexpr uint32_t kOffCnt  = 0x02u;   /* PIUCNTREG  (VR4121 20.3.1, VR4102 19.3.1) */
constexpr uint32_t kOffInt  = 0x04u;   /* PIUINTREG  (VR4121 20.3.2, VR4102 19.3.2) */
constexpr uint32_t kOffSivl = 0x06u;   /* PIUSIVLREG (VR4121 20.3.3, VR4102 19.3.3) */
constexpr uint32_t kOffStbl = 0x08u;   /* PIUSTBLREG (VR4121 20.3.4, VR4102 19.3.4) */
constexpr uint32_t kOffCmd  = 0x0Au;   /* PIUCMDREG  (VR4121 20.3.5, VR4102 19.3.5) */

/* PIUAB0REG at 0x0B0002B0, in the PIU's second window at 0x0B0002A0 (VR4121 UM 20.3.10,
   VR4102 UM 19.3.10). */
constexpr uint32_t kOffAb0 = 0x10u;

/* PIUCNTREG D12:10 PADSTATE "scan sequencer status" (VR4121 UM 20.3.1, VR4102 UM 19.3.1). */
enum : uint16_t {
    kStDisable      = 0,
    kStStandby      = 1,
    kStWaitPenTouch = 4,
    kStPenDataScan  = 5,
    kStIntervalNext = 6,
    kStCmdScan      = 7,
};

/* PIUCNTREG D9 PADATSTOP, D8 PADATSTART, D7 PADSCANSTOP, D6 PADSCANSTART, D5 PADSCANTYPE,
   D4:3 PIUMODE, D2 PIUSEQEN, D1 PIUPWR, D0 PADRST (VR4121 UM 20.3.1, VR4102 UM 19.3.1). */
enum : uint16_t {
    kPadRst = 0x0001, kPiuPwr = 0x0002, kSeqEn = 0x0004,
    kScanStart = 0x0040, kScanStop = 0x0080, kAtStart = 0x0100, kAtStop = 0x0200,
    kCntStored  = 0x033Eu,
};

/* PIUINTREG D15 OVP "valid page ID bit (older valid page)"; D6 PADCMDINTR, D5 PADADPINTR,
   D4 PADPAGE1INTR, D3 PADPAGE0INTR, D2 PADDLOSTINTR, D0 PENCHGINTR, each "cleared to 0 when
   1 is written" (VR4121 UM 20.3.2, VR4102 UM 19.3.2). */
enum : uint16_t {
    kPenChgIntr   = 0x0001, kPadDlostIntr = 0x0004, kPage0Intr = 0x0008,
    kPage1Intr    = 0x0010, kPadAdpIntr   = 0x0020, kPadCmdIntr = 0x0040,
    kIntCauses = kPenChgIntr | kPadDlostIntr | kPage0Intr | kPage1Intr |
                 kPadAdpIntr | kPadCmdIntr,
    kOvp = 0x8000u,
};

/* PIUPBnmREG / PIUABnREG D15 VALID + D9:0 PADDATA (VR4121 UM 20.3.9/20.3.10, VR4102 UM
   19.3.9/19.3.10). */
constexpr uint16_t kValid  = 0x8000u;
constexpr uint16_t kAdcMax = 1023u;

/* PIUSTBLREG D5:0 STABLE, both reset rows 0x0007 (VR4121 UM 20.3.4, VR4102 UM 19.3.4);
   PIUCMDREG D3:0 ADCMD, both reset rows 0x000F = "A/D converter standby mode request"
   (VR4121 UM 20.3.5, VR4102 UM 19.3.5). */
constexpr uint16_t kStblPowerOn = 0x0007u;
constexpr uint16_t kCmdPowerOn  = 0x000Fu;

struct Vr41xxPiuModel {
    uint32_t base;
    uint32_t size;
    uint32_t piu2_base;
    uint32_t piu2_size;
    uint16_t sivl_power_on;             /* PIUSIVLREG reset row                        */
    bool     has_penstp;                /* PIUCNTREG D14 PENSTP exists                 */
    bool     penstc_latched_by_penchg;  /* PIUCNTREG D13 PENSTC holds while PENCHGINTR */
    bool     page_buf_x_minus_first;    /* PIUPBn0 holds X- (VR4121 UM Table 20-4)     */
};
}  /* namespace cerf_vr41xx_piu_detail */
