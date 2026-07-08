#pragma once

#include <cstdint>

/* nk.bin (NEC MobilePro 700, CE 2.0). CRC32 over the loaded ROM bytes, printed
   by TraceManager::OnReady as "[TRACE] bundle CRC32 = 0x...". */
constexpr uint32_t kNecMobilePro700BundleCrc32 = 0xCB347113u;
