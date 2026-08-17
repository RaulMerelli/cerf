#pragma once

#include "imx6_ipu_internal_mem.h"

#include <cstdint>

struct Imx6IpuChannelDesc {
    uint32_t eba = 0;
    uint32_t eba0 = 0;
    uint32_t eba1 = 0;
    uint32_t fw = 0;
    uint32_t fh = 0;
    uint32_t sl = 0;
    uint32_t bpp_code = 0;
    uint32_t bits_per_pixel = 0;
    uint32_t bytes_per_pixel = 0;
    uint32_t pfs = 0;
    bool valid = false;
};

/* i.MX6 IPUv3: IPU1 registers are exposed at 0x02600000 and CPMEM at
   0x02700000 (the SoC's IPU1 ARB window starts at 0x02400000). */
class Imx6IpuCpmem : public cerf_imx6_ipu_mem_detail::Imx6IpuInternalMem<0x02700000u> {
public:
    using Imx6IpuInternalMem::Imx6IpuInternalMem;

    Imx6IpuChannelDesc DecodeChannel(uint32_t ch) const;
    uint32_t RawWord(uint32_t ch, uint32_t entry, uint32_t word) const {
        return regs_[ch * 16u + entry * 8u + word];
    }
    void SetActiveDisplayChannel(uint32_t ch) { active_display_channel_ = ch; }
    uint32_t ActiveDisplayChannel() const { return active_display_channel_; }
    void SetCurrentBuffer(uint32_t ch, uint32_t buffer) {
        if (ch < 64u) current_buffer_[ch] = buffer & 1u;
    }

private:
    uint32_t Field(uint32_t ch, uint32_t word, uint32_t off, uint32_t len) const;
    uint32_t active_display_channel_ = 0xFFFFFFFFu;
    uint8_t current_buffer_[64]{};
};
