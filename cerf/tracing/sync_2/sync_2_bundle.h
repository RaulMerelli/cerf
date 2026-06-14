#pragma once

#include <cstdint>

/* Ford SYNC Gen2 (i.MX51) NK.bin bundle CRC32, from the boot-time
   "[TRACE] bundle CRC32 = 0x..." line. Trace files in this directory gate
   on it and silently no-op on any other bundle. */
constexpr uint32_t kBundleCrc32 = 0x35873CBAu;
