#pragma once

#include <cstdint>

namespace imx6_vivante {

/* etnaviv rnndb/state_2d.xml DRAW_2D register offsets. */
constexpr uint32_t kD2dSrcAddress = 0x01200u;
constexpr uint32_t kD2dSrcStride = 0x01204u;
constexpr uint32_t kD2dSrcRotConfig = 0x01208u;
constexpr uint32_t kD2dSrcConfig = 0x0120Cu;
constexpr uint32_t kD2dSrcOrigin = 0x01210u;
constexpr uint32_t kD2dSrcSize = 0x01214u;
constexpr uint32_t kD2dSrcColorBg = 0x01218u;
constexpr uint32_t kD2dSrcColorFg = 0x0121Cu;
constexpr uint32_t kD2dStretchX = 0x01220u;
constexpr uint32_t kD2dStretchY = 0x01224u;
constexpr uint32_t kD2dDestAddress = 0x01228u;
constexpr uint32_t kD2dDestStride = 0x0122Cu;
constexpr uint32_t kD2dDestRotConfig = 0x01230u;
constexpr uint32_t kD2dDestConfig = 0x01234u;
constexpr uint32_t kD2dPatternAddress = 0x01238u;
constexpr uint32_t kD2dPatternConfig = 0x0123Cu;
constexpr uint32_t kD2dPatternLow = 0x01240u;
constexpr uint32_t kD2dPatternHigh = 0x01244u;
constexpr uint32_t kD2dPatternMaskLow = 0x01248u;
constexpr uint32_t kD2dPatternMaskHigh = 0x0124Cu;
constexpr uint32_t kD2dPatternBg = 0x01250u;
constexpr uint32_t kD2dPatternFg = 0x01254u;
constexpr uint32_t kD2dRop = 0x0125Cu;
constexpr uint32_t kD2dDestClipLow = 0x01260u;
constexpr uint32_t kD2dDestClipHigh = 0x01264u;
constexpr uint32_t kD2dClearByteMask = 0x01268u;
constexpr uint32_t kD2dClearPe10Lo = 0x01270u;
constexpr uint32_t kD2dClearPe10Hi = 0x01274u;
constexpr uint32_t kD2dAlphaControl = 0x0127Cu;
constexpr uint32_t kD2dAlphaModes = 0x01280u;
constexpr uint32_t kD2dDestRotHeight = 0x012B4u;
constexpr uint32_t kD2dSrcRotHeight = 0x012B8u;
constexpr uint32_t kD2dRotAngle = 0x012BCu;
constexpr uint32_t kD2dClearPe20 = 0x012C0u;
constexpr uint32_t kD2dDestColorKey = 0x012C4u;
constexpr uint32_t kD2dGlobalSrcColor = 0x012C8u;
constexpr uint32_t kD2dGlobalDstColor = 0x012CCu;
constexpr uint32_t kD2dColorMultiplyModes = 0x012D0u;
constexpr uint32_t kD2dPeTransparency = 0x012D4u;
constexpr uint32_t kD2dSrcColorKeyHigh = 0x012DCu;
constexpr uint32_t kD2dDestColorKeyHigh = 0x012E0u;
constexpr uint32_t kD2dSrcExConfig = 0x01300u;
constexpr uint32_t kD2dSrcExAddress = 0x01304u;
constexpr uint32_t kD2dMultiSource = 0x01308u;
constexpr uint32_t kD2dMultiSrcAddress = 0x12800u;
constexpr uint32_t kD2dMultiSrcStride = 0x12810u;
constexpr uint32_t kD2dMultiSrcRotConfig = 0x12820u;
constexpr uint32_t kD2dMultiSrcConfig = 0x12830u;
constexpr uint32_t kD2dMultiSrcOrigin = 0x12840u;
constexpr uint32_t kD2dMultiSrcSize = 0x12850u;
constexpr uint32_t kD2dMultiSrcColorBg = 0x12860u;
constexpr uint32_t kD2dMultiSrcRop = 0x12870u;
constexpr uint32_t kD2dMultiSrcAlphaControl = 0x12880u;
constexpr uint32_t kD2dMultiSrcAlphaModes = 0x12890u;
constexpr uint32_t kD2dMultiSrcRotHeight = 0x128E0u;
constexpr uint32_t kD2dMultiSrcRotAngle = 0x128F0u;
constexpr uint32_t kD2dMultiSrcGlobalSrcColor = 0x12900u;
constexpr uint32_t kD2dMultiSrcGlobalDstColor = 0x12910u;
constexpr uint32_t kD2dMultiSrcColorMultiply = 0x12920u;
constexpr uint32_t kD2dMultiSrcTransparency = 0x12930u;
constexpr uint32_t kD2dMultiSrcControl = 0x12940u;
constexpr uint32_t kD2dMultiSrcColorKeyHigh = 0x12950u;
constexpr uint32_t kD2dMultiSrcExConfig = 0x12960u;
constexpr uint32_t kD2dMultiSrcExAddress = 0x12970u;
constexpr uint32_t kD2dIndexColorTable32 = 0x03400u;

} // namespace imx6_vivante
