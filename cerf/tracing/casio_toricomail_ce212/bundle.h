#pragma once

#include <cstdint>

/* nk.bin (Casio Toricomail, CE 2.12). CRC32 over the loaded ROM bytes, printed
   by TraceManager::OnReady as "[TRACE] bundle CRC32 = 0x...". */
constexpr uint32_t kCasioToricomailBundleCrc32 = 0xD8199B0Fu;
