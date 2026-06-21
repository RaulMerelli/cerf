#pragma once

#include <cstdint>

/* simpad_sl4_hpc2000 (Siemens SIMpad SL4, HPC2000 / CE 3.0 ROM
   S842-SI-INT-008-SL4.nb0). CRC32 over the concatenated loaded partition raw
   bytes - read from the boot log line "[TRACE] bundle CRC32 = 0x824B1021". */
constexpr uint32_t kBundleCrc32 = 0x824B1021u;
