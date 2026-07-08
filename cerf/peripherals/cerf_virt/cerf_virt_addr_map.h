#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#define CERF_VIRT_GUEST 1
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kBaseMagic = 0xCEF0BA5Eu;

const uint32_t kTotalSize = 0x10000000u;

const uint32_t kRegsOffset = 0u;
const uint32_t kRegsSize   = 0x10000u;

const uint32_t kFramebufferRegsOffset = 0x1000u;
const uint32_t kFramebufferRegsSize   = 0x1000u;

const uint32_t kGpeCmdOffset = 0x2000u;
const uint32_t kGpeCmdSize   = 0x1000u;

const uint32_t kPointerOffset = 0x3000u;
const uint32_t kPointerSize   = 0x1000u;

const uint32_t kCursorOffset = 0x4000u;
const uint32_t kCursorSize   = 0x1000u;

const uint32_t kFolderShareOffset = 0x5000u;
const uint32_t kFolderShareSize   = 0x1000u;

const uint32_t kResizeOffset = 0x6000u;
const uint32_t kResizeSize   = 0x1000u;

const uint32_t kLogChannelOffset = 0x7000u;
const uint32_t kLogChannelStride = 0x1000u;
const uint32_t kLogChannelIdStub          = 0u;
const uint32_t kLogChannelIdDisplay       = 1u;
const uint32_t kLogChannelIdSharedFolders = 2u;
const uint32_t kLogChannelCount  = 3u;
const uint32_t kLogChannelSize   = kLogChannelCount * kLogChannelStride;

const uint32_t kTaskManagerOffset = 0xA000u;
const uint32_t kTaskManagerSize   = 0x1000u;

const uint32_t kKeyboardOffset = 0xB000u;
const uint32_t kKeyboardSize   = 0x1000u;

const uint32_t kPaletteOffset  = 0xC000u;
const uint32_t kPaletteSize    = 0x1000u;
const uint32_t kPaletteEntries = 256u;

const uint32_t kFramebufferMemOffset = 0x00100000u;
const uint32_t kFramebufferMemSize   = 0x02000000u;

const uint32_t kGuestBodyOffset  = kRegsSize;
const uint32_t kGuestBodyHdrSize = 0x1000u;

const uint32_t kInjectionBandSize   = 0x10000u;
const uint32_t kInjectionBandOffset = kFramebufferMemOffset - kInjectionBandSize;

const uint32_t kGuestBodyMaxSize = kInjectionBandOffset - kGuestBodyOffset;

}  /* namespace CerfVirt */

#ifdef CERF_VIRT_GUEST
extern volatile unsigned long g_CerfVirtBase;
#endif
