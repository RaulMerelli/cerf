#pragma once

#include <cstdint>

constexpr uint32_t MB(uint32_t mb) {
    return mb * 0x100000u;
}

/* The cached DDR window CERF places a KTP Mobile image in.  This is CERF's own
   placement, not an OAL OEMAddressTable entry: the peripheral spans of that
   table are read from the running ROM (ktp_mobile_oat_from_rom.h), because the
   V13 and V17 panels declare different ones. */
constexpr uint32_t kDramVa = 0x80000000u;
constexpr uint32_t kDramPa = 0x10000000u;
constexpr uint32_t kDramSize = MB(384);

/* Boot-handoff stack: top of that window. */
constexpr uint32_t kInitStackTopPa = kDramPa + kDramSize;
