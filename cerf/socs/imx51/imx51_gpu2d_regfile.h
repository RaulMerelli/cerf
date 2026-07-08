#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

/* Float and device-scale reads off the g12 command-engine register file, shared
   by the geometry decode (command engine) and the VG-FLUSH fill dispatch. */
namespace imx51_g2d_regfile {

/* Reinterpret a register entry as its float payload (XF affine, VGV2 SCALE/BIAS,
   GRADW CONST all live in the file as IEEE-754 bit patterns). */
inline float RegF(const uint32_t (&regs)[0x100], uint32_t reg) {
    float f;
    std::memcpy(&f, &regs[reg], sizeof(f));
    return f;
}

/* 2^VGV2_MODE.EXPONENTADD[23:18] (signed 6-bit): the XF affine outputs subpixels
   that the driver premultiplies by this power of two (fill engine sub_41C63970). */
inline float DeviceScale(const uint32_t (&regs)[0x100]) {
    const int32_t expadd = static_cast<int32_t>(regs[0x6Eu] << 8) >> 26;
    return std::ldexp(1.0f, expadd);
}

/* GRADW paint CONST are a g12-custom 16-bit float (encoder csi_stream_regwrite5f10,
   0x41C961F0): sign[15]|exp[14:10] two's-complement UNBIASED|mantissa[9:0], exp==-16
   (field 0x10) = zero. A 32-bit RegF reinterpret reads a denormal ~0 (silent mis-render). */
inline float UnpackGradwConst(uint32_t v) {
    const uint32_t e5 = (v >> 10) & 0x1Fu;
    if (e5 == 0x10u) return 0.0f;
    const int32_t exp = (e5 >= 0x10u) ? static_cast<int32_t>(e5) - 32 : static_cast<int32_t>(e5);
    const float val = std::ldexp(1.0f + static_cast<float>(v & 0x3FFu) / 1024.0f, exp);
    return (v & 0x8000u) ? -val : val;
}

}  // namespace imx51_g2d_regfile
