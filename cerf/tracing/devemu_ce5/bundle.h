#pragma once

#include <cstdint>

/* Bundle CRC32 for devemu_ce5 (Microsoft Device Emulator, Windows CE 5.0).
   Logged by RomParserService over the loaded ROM's raw bytes; trace files in
   this directory key off it via TraceManager::RegisterForBundle. */
inline constexpr uint32_t kDevEmuCe5BundleCrc32 = 0xEB21C0CBu;
