#pragma once

#include <cstdint>

namespace siemens_mp377 {

/* P377 OAL local-bus config-cycle normalization encodes dev14/fn0 as
   0x40007000.  dev15/fn0 becomes 0x80007800, and Iop13xxAtuConfig masks bit31
   to match the SM501 select 0x00007800; dev14 keeps bit30 set and therefore
   must not be compared as plain 0x00007000. */
constexpr uint32_t kErtec400PciSelect = 0x40007000u;  /* bus0/dev14/fn0 */

constexpr uint32_t kErtecBar0Base = 0xC5000000u;
constexpr uint32_t kErtecBar1Base = 0xC5010000u;
constexpr uint32_t kErtecBar2Base = 0xC5020000u;
constexpr uint32_t kErtecBar4Base = 0xC5030000u;
constexpr uint32_t kErtecBar5Base = 0xC5040000u;
constexpr uint32_t kErtecSmallBarsBase = kErtecBar0Base;
/* The MP377 OAT maps the eddertec400.dll virtual BARs directly into the
   0xC0000000 PCI outbound window: B9000000 -> C5000000, B9800000 -> C5800000.
   There is no extra static alias at +1 MB.  ERTEC400 exposes the IRT register
   area in the first 1 MB and communication RAM in the second 1 MB of the
   8 MB IRT aperture; BAR0/BAR1/BAR2 are 64 KB PCI resources, but the driver
   reaches BAR0+0x1xxxxx during the fatal/error path, so the modeled BAR0
   window must cover the documented first 2 MB aperture rather than collapsing
   it onto BAR0+0. */
constexpr uint32_t kErtecIrtApertureSize = 0x00200000u;
/* Some P377 static MMU paths present ERTEC register accesses one megabyte
   above the PCI BAR base.  Do not collapse the whole second megabyte: ERTEC400
   documents it as communication RAM.  Only the driver-visible register offsets
   decoded below are mirrored. */
constexpr uint32_t kErtecRegisterAliasDelta = 0x00100000u;
constexpr uint32_t kErtecSmallBarsEnd  = kErtecSmallBarsBase + kErtecIrtApertureSize;
constexpr uint32_t kErtecSmallBarsSize = kErtecSmallBarsEnd - kErtecSmallBarsBase;

constexpr uint32_t kErtecEddPhyModeOffset = 0x00019038u;
constexpr uint32_t kErtecEddHwTypeOffset  = 0x00019400u;
constexpr uint32_t kErtecEddHwTypeErtec400Rev5 = 0x20050000u;

constexpr uint32_t kErtecResetControlOffset = 0x0001260Cu;
constexpr uint32_t kErtecBootReadyOffset    = 0x00101020u;
constexpr uint32_t kErtecBootReadyBit       = 0x00000001u;
constexpr uint32_t kErtecSwiControlOffset   = kErtecEddPhyModeOffset;
constexpr uint32_t kErtecSwiStatusOffset    = 0x00019404u;
constexpr uint32_t kErtecSwiStatusAllDone   = 0x0000FFFFu;
constexpr uint32_t kErtecSwiStatusMinMode   = 0x0000FFFAu;
constexpr uint32_t kErtecConsResetBaseOffset = 0x00011000u;
constexpr uint32_t kErtecIrtTimerBaseOffset  = 0x0000B000u;
constexpr uint32_t kErtecIrtControlOffset    = 0x00013000u;
constexpr uint32_t kErtecIrtStartOffset      = 0x00018400u;
constexpr uint32_t kErtecFlowControlOffset   = 0x00016410u;

/* eddertec400.dll requests raw IRQ 0x1A through IOCTL_HAL_REQUEST_SYSINTR.
   Its IST reads the ERTEC event words at +0x17418/+0x1741C, then sub_28DFF28
   swaps them before sub_28DFC54 dispatches sources 32..63.  Source 41 signals
   the link-status worker, so bit 0x200 belongs in the high event word. */
constexpr int      kErtecIrqSource             = 0x1A;
constexpr uint32_t kErtecIrqStatusLoOffset     = 0x00017418u;
constexpr uint32_t kErtecIrqStatusHiOffset     = 0x0001741Cu;
constexpr uint32_t kErtecIrqAckOffset          = 0x00017420u;
constexpr uint32_t kErtecIrqLinkChangeHiBit    = 0x00000200u;

constexpr uint32_t kErtecSerPrimCommandOffset = 0x00016400u;
constexpr uint32_t kErtecSerSecCommandOffset  = 0x00016404u;
constexpr uint32_t kErtecSerConfCommandOffset = 0x00016408u;
constexpr uint32_t kErtecSerCommandActiveBit  = 0x80000000u;
constexpr uint32_t kErtecSerCommandOkBit      = 0x40000000u;

/* SERSetupNRT programs four NRT DMACW blocks.  Each block has a command
   register at base+0x0c*n; the driver writes 6 and polls until bit1 clears. */
constexpr uint32_t kErtecNrtDmacBaseOffset   = 0x00012400u;
constexpr uint32_t kErtecNrtDmacStride       = 0x0000000Cu;
constexpr uint32_t kErtecNrtDmacPortCount    = 4u;

constexpr uint32_t kErtecSmallWindowBase = kErtecSmallBarsBase;
constexpr uint32_t kErtecSmallWindowEnd  = kErtecSmallBarsEnd;
constexpr uint32_t kErtecSmallWindowSize = kErtecSmallWindowEnd - kErtecSmallWindowBase;

constexpr uint32_t kErtecBar3Base = 0xC5800000u;
constexpr uint32_t kErtecBar3Size = 0x00800000u;
constexpr uint32_t kErtecBar3End  = kErtecBar3Base + kErtecBar3Size;
constexpr uint32_t kErtecBar3WindowBase = kErtecBar3Base;
constexpr uint32_t kErtecBar3WindowEnd  = kErtecBar3End;
constexpr uint32_t kErtecBar3WindowSize = kErtecBar3WindowEnd - kErtecBar3WindowBase;

}  /* namespace siemens_mp377 */

