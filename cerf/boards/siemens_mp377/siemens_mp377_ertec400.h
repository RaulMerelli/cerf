#pragma once

#include <cstdint>

namespace siemens_mp377 {

constexpr uint32_t kErtec400PciSelect = 0x40007000u;

constexpr uint32_t kErtecBar0Base = 0xC5000000u;
constexpr uint32_t kErtecBar1Base = 0xC5010000u;
constexpr uint32_t kErtecBar2Base = 0xC5020000u;
constexpr uint32_t kErtecBar4Base = 0xC5030000u;
constexpr uint32_t kErtecBar5Base = 0xC5040000u;
constexpr uint32_t kErtecSmallBarsBase = kErtecBar0Base;
/* siemens_mp377_v1040 eddertec400.dll: the MP377 OAT maps the virtual BARs directly into the
   0xC0000000 PCI outbound window: B9000000 -> C5000000, B9800000 -> C5800000.
   There is no extra static alias at +1 MB.  ERTEC400 exposes the IRT register
   area in the first 1 MB and communication RAM in the second 1 MB of the
   8 MB IRT aperture; BAR0/BAR1/BAR2 are 64 KB PCI resources, but the driver
   reaches BAR0+0x1xxxxx during the fatal/error path, so the modeled BAR0
   window must cover the documented first 2 MB aperture rather than collapsing
   it onto BAR0+0. */
constexpr uint32_t kErtecIrtApertureSize = 0x00200000u;
/* ERTEC400 Manual V1.2.2 section 10.1.1: KRAM 0x10100000..0x1012FFFF. */
constexpr uint32_t kErtecCommunicationRamBase = 0x00100000u;
constexpr uint32_t kErtecCommunicationRamSize = 0x00030000u;
constexpr uint32_t kErtecCommunicationRamEnd = kErtecCommunicationRamBase + kErtecCommunicationRamSize;
constexpr uint32_t kErtecSmallBarsEnd = kErtecSmallBarsBase + kErtecIrtApertureSize;
constexpr uint32_t kErtecSmallBarsSize = kErtecSmallBarsEnd - kErtecSmallBarsBase;

constexpr uint32_t kErtecEddPhyModeOffset = 0x00019038u;
constexpr uint32_t kErtecEddHwTypeOffset = 0x00019400u;
constexpr uint32_t kErtecEddHwTypeErtec400Rev5 = 0x20050000u;

constexpr uint32_t kErtecResetControlOffset = 0x0001260Cu;
constexpr uint32_t kErtecBootReadyOffset = 0x00101020u;
constexpr uint32_t kErtecBootReadyBit = 0x00000001u;
constexpr uint32_t kErtecSwiControlOffset = kErtecEddPhyModeOffset;
constexpr uint32_t kErtecSwiStatusOffset = 0x00019404u;
constexpr uint32_t kErtecSwiStatusAllDone = 0x0000FFFFu;
constexpr uint32_t kErtecSwiStatusMinMode = 0x0000FFFAu;
constexpr uint32_t kErtecConsResetBaseOffset = 0x00011000u;
constexpr uint32_t kErtecIrtTimerBaseOffset = 0x0000B000u;
constexpr uint32_t kErtecIrtControlOffset = 0x00013000u;
constexpr uint32_t kErtecIrtStartOffset = 0x00018400u;
constexpr uint32_t kErtecFlowControlOffset = 0x00016410u;

/* siemens_mp377_v1040 eddertec400.dll requests raw IRQ 0x1A through IOCTL_HAL_REQUEST_SYSINTR.
   Its IST reads the ERTEC event words at +0x17418/+0x1741C, then sub_28DFF28
   swaps them before sub_28DFC54 dispatches sources 32..63.  Source 41 signals
   the link-status worker, so bit 0x200 belongs in the high event word. */
constexpr int kErtecIrqSource = 0x1A;
constexpr uint32_t kErtecIrqStatusLoOffset = 0x00017418u;
constexpr uint32_t kErtecIrqStatusHiOffset = 0x0001741Cu;
constexpr uint32_t kErtecIrqAckOffset = 0x00017420u;
constexpr uint32_t kErtecIrqLinkChangeHiBit = 0x00000200u;

constexpr uint32_t kErtecSerPrimCommandOffset = 0x00016400u;
constexpr uint32_t kErtecSerSecCommandOffset = 0x00016404u;
constexpr uint32_t kErtecSerConfCommandOffset = 0x00016408u;
constexpr uint32_t kErtecSerCommandActiveBit = 0x80000000u;
constexpr uint32_t kErtecSerCommandOkBit = 0x40000000u;

/* siemens_mp377_v1040 eddertec400.dll SERSetupNRT. */
constexpr uint32_t kErtecNrtDmacBaseOffset = 0x00012400u;
constexpr uint32_t kErtecNrtDmacStride = 0x0000000Cu;
constexpr uint32_t kErtecNrtDmacPortCount = 4u;

constexpr uint32_t kErtecSmallWindowBase = kErtecSmallBarsBase;
constexpr uint32_t kErtecSmallWindowEnd = kErtecSmallBarsEnd;
constexpr uint32_t kErtecSmallWindowSize = kErtecSmallWindowEnd - kErtecSmallWindowBase;

constexpr uint32_t kErtecBar3Base = 0xC5800000u;
constexpr uint32_t kErtecBar3Size = 0x00800000u;
constexpr uint32_t kErtecBar3End = kErtecBar3Base + kErtecBar3Size;
constexpr uint32_t kErtecBar3WindowBase = kErtecBar3Base;
constexpr uint32_t kErtecBar3WindowEnd = kErtecBar3End;
constexpr uint32_t kErtecBar3WindowSize = kErtecBar3WindowEnd - kErtecBar3WindowBase;

} /* namespace siemens_mp377 */
