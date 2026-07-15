#define NOMINMAX

#include "cerf_virt_framebuffer.h"

#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/frame_renderer.h"

#include <algorithm>
#include <cstring>

namespace {

class CerfVirtFramebufferRenderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    bool HasFrame() override {
        return emu_.Get<CerfVirtFramebuffer>().HasContent();
    }

    void RearmContentLatch() override {
        emu_.Get<CerfVirtFramebuffer>().ClearContentEdge();
    }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  host_w,
                    uint32_t  host_h) override {
        auto& fb = emu_.Get<CerfVirtFramebuffer>();
        const uint32_t guest_w = fb.Width();
        const uint32_t guest_h = fb.Height();
        const uint8_t* src = fb.Bytes();

        std::memset(dib_bgra32, 0,
                    static_cast<size_t>(host_w) * host_h * 4u);

        const uint32_t copy_w = std::min(guest_w, host_w);
        const uint32_t copy_h = std::min(guest_h, host_h);
        if (copy_w == 0 || copy_h == 0) return;

        const uint32_t guest_stride = fb.Stride();
        const uint32_t bpp = fb.Bpp();
        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint8_t* src_row = src + static_cast<size_t>(y) * guest_stride;
            uint32_t* dst_row = dib_bgra32 + static_cast<size_t>(y) * host_w;
            if (bpp == 32u) {
                std::memcpy(dst_row, src_row, static_cast<size_t>(copy_w) * 4u);
            } else if (bpp == 8u) {
                const uint32_t* pal = fb.Palette();
                for (uint32_t x = 0; x < copy_w; ++x)
                    dst_row[x] = 0xFF000000u | pal[src_row[x]];
            } else if (bpp == 16u) {
                const uint16_t* s = reinterpret_cast<const uint16_t*>(src_row);
                for (uint32_t x = 0; x < copy_w; ++x) {
                    const uint32_t p  = s[x];
                    const uint32_t r5 = (p >> 11) & 0x1Fu;
                    const uint32_t g6 = (p >> 5)  & 0x3Fu;
                    const uint32_t b5 =  p        & 0x1Fu;
                    dst_row[x] = 0xFF000000u
                               | (((r5 << 3) | (r5 >> 2)) << 16)
                               | (((g6 << 2) | (g6 >> 4)) << 8)
                               |  ((b5 << 3) | (b5 >> 2));
                }
            } else if (bpp == 24u) {
                for (uint32_t x = 0; x < copy_w; ++x) {
                    const uint8_t* p = src_row + static_cast<size_t>(x) * 3u;
                    dst_row[x] = 0xFF000000u
                               | (static_cast<uint32_t>(p[2]) << 16)
                               | (static_cast<uint32_t>(p[1]) << 8)
                               |  static_cast<uint32_t>(p[0]);
                }
            }
        }
    }
};

REGISTER_SERVICE_AS(CerfVirtFramebufferRenderer, FrameRenderer);

}
