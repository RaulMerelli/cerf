#pragma once

#include <cstdint>

/* On-ROM record layouts from romldr.h / pehdr.h: ROMHDR, TOCentry,
   e32_rom, o32_rom field offsets as romimage burns them. */

struct E32RomLayout {
    uint32_t size;
    uint32_t off_objcnt;
    uint32_t off_imageflags;
    uint32_t off_entryrva;
    uint32_t off_vbase;
    uint32_t off_subsysmajor;
    uint32_t off_subsysminor;
    uint32_t off_stackmax;
    uint32_t off_vsize;
    int32_t  off_sect14rva;     /* absent on CE2 → negative (added in CE3) */
    int32_t  off_sect14size;    /* absent on CE2 → negative (added in CE3) */
    int32_t  off_timestamp;     /* absent on CE3 → negative */
    uint32_t off_unit;
    uint32_t off_subsys;
};

/* CE 2.x e32_rom (WINCE211/PUBLIC/COMMON/OAK/INC/ROMLDR.H): e32_subsys is a
   u16 at 0x18 BEFORE the DD array, there is no e32_sect14 field (added in CE3)
   and no timestamp. e32_subsys(0x18,u16)+2 pad, then e32_unit[9] at 0x1C,
   so sizeof(e32_rom)=0x64=100. */
constexpr E32RomLayout kE32RomCE2 = {
    100,
    0x00, 0x02, 0x04, 0x08,
    0x0C, 0x0E, 0x10, 0x14,
    -1, -1,
    -1,
    0x1C, 0x18,
};

constexpr E32RomLayout kE32RomCE3 = {
    106,
    0x00, 0x02, 0x04, 0x08,
    0x0C, 0x0E, 0x10, 0x14,
    0x18, 0x1C,
    -1,
    0x20, 0x68,
};

constexpr E32RomLayout kE32RomCE5plus = {
    110,
    0x00, 0x02, 0x04, 0x08,
    0x0C, 0x0E, 0x10, 0x14,
    0x18, 0x1C,
    0x20,
    0x24, 0x6C,
};

/* e32_subsysmajor lies at +0x0C in BOTH CE3 and CE5+ layouts —
   safe to read before the layout decision is made. */
constexpr uint32_t kE32SubsysmajorOff = 0x0C;

constexpr int kE32UnitCount = 9;

constexpr uint32_t kRomHdrSize    = 84;
constexpr uint32_t kTocEntrySize  = 32;

constexpr uint32_t kO32RomSize    = 24;
constexpr uint32_t kO32OffVsize    = 0;
constexpr uint32_t kO32OffRva      = 4;
constexpr uint32_t kO32OffPsize    = 8;
constexpr uint32_t kO32OffDataptr  = 12;
constexpr uint32_t kO32OffRealaddr = 16;
constexpr uint32_t kO32OffFlags    = 20;

/* ROMHDR field offsets. */
constexpr uint32_t kHdrDllFirstOff   = 0x00;
constexpr uint32_t kHdrRAMFreeOff    = 0x18;

/* TOCentry field offsets. */
constexpr uint32_t kTocOffNFileSize  = 0x0C;
constexpr uint32_t kTocOffE32Offset  = 0x14;
constexpr uint32_t kTocOffO32Offset  = 0x18;
constexpr uint32_t kTocOffLoadOffset = 0x1C;
