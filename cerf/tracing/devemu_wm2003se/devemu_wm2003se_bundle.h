#pragma once

#include <cstdint>

/* devemu_wm2003se "WM2003SE.bin" - CRC32 over the concatenated loaded
   partition bytes, reported by TraceManager::OnReady as
   "[TRACE] bundle CRC32 = 0xF0BB8616". */
constexpr uint32_t kDevemuWm2003seBundleCrc32 = 0xF0BB8616u;
