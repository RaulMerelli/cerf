#pragma once

#include "siemens_mp377_sm501.h"

#include <cstdint>

namespace siemens_mp377 {

/* SM501 MMCC Databook v1.02, Tables 2-1, 4-1, 5-1 and 16-1. */
inline bool Sm501IsPlainRegister(uint32_t off) {
    switch (off) {
    case 0x000008u:
    case 0x000030u:
    case 0x000054u:
    case 0x000058u:
    case 0x000068u:
    case 0x020000u:
    case 0x020004u:
    case 0x020008u:
    case 0x020010u:
    case 0x080004u:
    case 0x080008u:
    case 0x08000Cu:
    case 0x080010u:
    case 0x080014u:
    case 0x080024u:
    case 0x08002Cu:
    case 0x08005Cu:
    case 0x0800F0u:
    case 0x0800F4u:
    case 0x0800F8u:
    case 0x0800FCu:
    case 0x100000u:
    case 0x100004u:
    case 0x100008u:
    case 0x10000Cu:
    case 0x100010u:
    case 0x100014u:
    case 0x100018u:
    case 0x10001Cu:
    case 0x100020u:
    case 0x100024u:
    case 0x100028u:
    case 0x10002Cu:
    case 0x100034u:
    case 0x100038u:
    case 0x10003Cu:
    case 0x100040u:
    case 0x100044u:
    case 0x100048u:
    case 0x10004Cu: return true;
    default: return off >= 0x110000u && off < 0x110100u;
    }
}

inline bool Sm501In2dWindow(uint32_t off) {
    return (off >= 0x100000u && off < 0x100050u) || (off >= 0x1000C8u && off < 0x100100u) ||
           (off >= 0x110000u && off < 0x111000u);
}

inline bool Sm501Native2dWrite(uint32_t off) {
    return Sm501In2dWindow(off) && off != 0x100030u;
}

inline bool Sm501Native2dRead(uint32_t off) {
    return Sm501In2dWindow(off) && off != 0x100008u && off != 0x100024u && off != 0x10002Cu && off != 0x100030u;
}

inline uint32_t Sm501FoldWrite(uint32_t address) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(address, off)) return address;
    return off >= 0x100000u && off < 0x200000u && !Sm501Native2dWrite(off) ? address - 0x100000u : address;
}

inline uint32_t Sm501FoldRead(uint32_t address) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(address, off)) return address;
    return off >= 0x100000u && off < 0x200000u && !Sm501Native2dRead(off) ? address - 0x100000u : address;
}

} // namespace siemens_mp377
