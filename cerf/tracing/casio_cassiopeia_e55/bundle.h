#pragma once

#include <cstdint>

/* nk.bin (Casio Cassiopeia E-55, Palm-size PC 1.2 / CE 2.11). CRC32 over the
   loaded ROM bytes, printed by TraceManager::OnReady as
   "[TRACE] bundle CRC32 = 0x...". */
constexpr uint32_t kCasioCassiopeiaE55BundleCrc32 = 0x445F23DCu;
