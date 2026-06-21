#pragma once

#include <cstdint>

/* CRC32 over the concatenated ROM partitions in load order - printed as the
   "[TRACE] bundle CRC32 = 0x..." line on the first boot of this bundle. */
constexpr uint32_t kSiemensTp177bBundleCrc32 = 0x48CF870Cu;
