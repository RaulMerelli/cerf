#pragma once

/* Bundle CRC32 for wm5_smdk2410_devemu - computed by RomParserService
   over the concatenated raw bytes of every loaded ROM partition (just
   WM5_PPC_USA.BIN in this device's case). All trace files in this
   directory key off this single value via TraceManager::RegisterForBundle. */
inline constexpr uint32_t kWm5BundleCrc32 = 0x3C5A7C4Fu;
