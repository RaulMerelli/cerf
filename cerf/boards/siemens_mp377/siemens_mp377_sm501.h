#pragma once

#include <cstdint>

class CerfEmulator;

namespace siemens_mp377 {

inline constexpr uint32_t kFbPa      = 0xCA000000u;
inline constexpr uint32_t kFbBytes   = 0x01000000u;
inline constexpr uint32_t kRegPa     = 0xC8000000u;
inline constexpr uint32_t kRegBytes  = 0x00200000u;
inline constexpr uint32_t kFbWidth   = 640u;
inline constexpr uint32_t kFbHeight  = 480u;
inline constexpr uint32_t kFbStride  = kFbWidth * 2u;

const uint8_t* Sm501Vram(CerfEmulator& emu);
bool Sm501WasWritten(CerfEmulator& emu);
uint32_t Sm501PanelFbOffset(CerfEmulator& emu);
uint32_t Sm501PanelPitchBytes(CerfEmulator& emu);

uint32_t Mp377SmiBridgeRead(uint32_t pa);
void Mp377SmiBridgeWrite(uint32_t pa, uint32_t value);
void Mp377SmiBindEmulator(CerfEmulator* emu);

} // namespace siemens_mp377
