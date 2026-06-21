#pragma once

#include <cstdint>

/* devemu_ce5 "Windows CE 5.bin" - CRC32 over the concatenated loaded
   partition bytes, reported by TraceManager::OnReady as
   "[TRACE] bundle CRC32 = 0xEB21C0CB". */
constexpr uint32_t kDevemuCe5BundleCrc32 = 0xEB21C0CBu;
