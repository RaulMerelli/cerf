#include "imx6_ipu_cpmem.h"

#include <cstdint>

uint32_t Imx6IpuCpmem::Field(uint32_t ch, uint32_t word, uint32_t off, uint32_t len) const {
    const uint32_t base = ch * 16u + word * 8u;   /* 64 B/channel, 32 B/word */
    const uint32_t i = base + (off >> 5);
    const uint32_t bit = off & 31u;
    uint64_t v = static_cast<uint64_t>(regs_[i]) >> bit;
    if (bit + len > 32u)
        v |= static_cast<uint64_t>(regs_[i + 1]) << (32u - bit);
    return static_cast<uint32_t>(v & ((1ull << len) - 1ull));
}

Imx6IpuChannelDesc Imx6IpuCpmem::DecodeChannel(uint32_t ch) const {
    Imx6IpuChannelDesc d;
    d.eba0 = Field(ch, 1, 0, 29) << 3;
    d.eba1 = Field(ch, 1, 29, 29) << 3;
    d.eba = (ch < 64u && current_buffer_[ch] && d.eba1) ? d.eba1 : d.eba0;
    d.fw  = Field(ch, 0, 125, 13) + 1u;
    d.fh  = Field(ch, 0, 138, 12) + 1u;
    d.sl  = Field(ch, 1, 102, 14) + 1u;
    d.bpp_code = Field(ch, 0, 107, 3);
    switch (d.bpp_code) {
    case 0: d.bits_per_pixel = 32u; break;
    case 1: d.bits_per_pixel = 24u; break;
    case 3: d.bits_per_pixel = 16u; break;
    case 5: d.bits_per_pixel = 8u; break;
    default: d.bits_per_pixel = 0u; break;
    }
    d.bytes_per_pixel = (d.bits_per_pixel + 7u) / 8u;
    d.pfs = Field(ch, 1, 85, 4);
    d.valid = d.eba != 0u && d.fw > 1u && d.fh > 1u &&
              d.fw <= 4096u && d.fh <= 4096u && d.sl <= 32768u &&
              d.bytes_per_pixel != 0u;
    return d;
}

REGISTER_SERVICE(Imx6IpuCpmem);
