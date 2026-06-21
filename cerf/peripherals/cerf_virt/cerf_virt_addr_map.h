#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

/* MUST be a PA free on every supported board - cerf_virt registers here, so a
   board that maps this range faults at PeripheralDispatcher::Register. Highest
   PA any board maps is the 0xE0000000 zero-bank; 0xF0000000 is above them all. */

namespace CerfVirt {

const uint32_t kBaseAddr  = 0xF0000000u;
const uint32_t kTotalSize = 0x10000000u;

const uint32_t kRegsBase = kBaseAddr;
const uint32_t kRegsSize = 0x10000u;

/* 0x0000 page: unused. */

const uint32_t kFramebufferRegsBase = kRegsBase + 0x1000u;
const uint32_t kFramebufferRegsSize = 0x1000u;

const uint32_t kGpeCmdBase = kRegsBase + 0x2000u;
const uint32_t kGpeCmdSize = 0x1000u;

const uint32_t kPointerBase = kRegsBase + 0x3000u;
const uint32_t kPointerSize = 0x1000u;

const uint32_t kCursorBase = kRegsBase + 0x4000u;
const uint32_t kCursorSize = 0x1000u;

const uint32_t kFolderShareBase = kRegsBase + 0x5000u;
const uint32_t kFolderShareSize = 0x1000u;

const uint32_t kResizeBase = kRegsBase + 0x6000u;
const uint32_t kResizeSize = 0x1000u;

const uint32_t kLogChannelBase   = kRegsBase + 0x7000u;
const uint32_t kLogChannelStride = 0x1000u;
const uint32_t kLogChannelIdStub          = 0u;   /* cerf_guest_stub injection carrier */
const uint32_t kLogChannelIdDisplay       = 1u;   /* gwes display driver */
const uint32_t kLogChannelIdSharedFolders = 2u;   /* device.exe shared-folders FSD carrier */
const uint32_t kLogChannelCount  = 3u;
const uint32_t kLogChannelSize   = kLogChannelCount * kLogChannelStride;

/* Moved 0x9000 -> 0xA000: the 3-slot log block now occupies 0x7000..0x9000. */
const uint32_t kTaskManagerBase = kRegsBase + 0xA000u;
const uint32_t kTaskManagerSize = 0x1000u;

const uint32_t kKeyboardBase = kRegsBase + 0xB000u;
const uint32_t kKeyboardSize = 0x1000u;

const uint32_t kFramebufferMemBase = kBaseAddr + 0x00100000u;
const uint32_t kFramebufferMemSize = 0x02000000u;

/* cerf_guest body delivery: word[0] = body byte count, body bytes at
   +kGuestBodyHdrSize. The framebuffer-mem region grows UP from
   kFramebufferMemBase to fill the window (sized to the host monitor), so the
   body sits in the fixed gap BELOW it, just past the register pages. */
const uint32_t kGuestBodyBase    = kRegsBase + kRegsSize;
const uint32_t kGuestBodyHdrSize = 0x1000u;

/* Injected-stub band PA (overlay-mapped to a static-window VA so the stub's
   bytes live here, not in the victim's section). Carved BELOW
   kFramebufferMemBase - placing it above would be overrun by the
   host-monitor-sized framebuffer region that grows up from there. */
const uint32_t kInjectionBandSize = 0x10000u;
const uint32_t kInjectionBandBase = kFramebufferMemBase - kInjectionBandSize;

const uint32_t kGuestBodyMaxSize = kInjectionBandBase - kGuestBodyBase;

}  /* namespace CerfVirt */
