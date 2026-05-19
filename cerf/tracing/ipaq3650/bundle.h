#pragma once

#include <cstdint>

/* Bundle CRC32 for ppc2000_ipaq3650 (Compaq iPAQ H3650, IPAQROM177.nb0,
   PocketPC2000 / Windows CE 3.0). Computed by RomParserService over the
   concatenated raw bytes of every loaded ROM partition. Trace files in
   this directory key off this value via TraceManager::RegisterForBundle. */
inline constexpr uint32_t kIpaq3650BundleCrc32 = 0x9C71F6CAu;
