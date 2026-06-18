#pragma once

#include <cstdint>

/* SmartBook G138, Windows CE .NET 4.2 (ROM v2.15, 2004-06-14), English.
   CRC32 over the concatenated loaded ROM partitions, from the
   "[TRACE] bundle CRC32 = 0x..." boot line. */
constexpr uint32_t kBundleCrc32 = 0x478DC0C1u;
