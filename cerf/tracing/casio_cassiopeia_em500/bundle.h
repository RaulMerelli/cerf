#pragma once

#include <cstdint>

/* em500.bin (Casio Cassiopeia EM-500, Pocket PC 2000 / CE 3.0). CRC32 over the
   loaded ROM bytes, printed by TraceManager::OnReady as
   "[TRACE] bundle CRC32 = 0x...". */
constexpr uint32_t kCasioEm500BundleCrc32 = 0xB4699B05u;
