#define NOMINMAX

#include "mediaq_mq200.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/frame_renderer.h"

#include <cstring>

namespace {

constexpr size_t kContentProbeStride = 251;

/* MQ-200 pixel decode: 16-bpp direct = RGB565 (Data Book Table 5-8 depth 0xC),
   <=8-bpp index the Color Palette (Table 5-32: R[7:0] G[15:8] B[23:16]). 24/32-bpp
   direct read the framebuffer in Windows CE DIB byte order (B,G,R[,X]). */
class MediaQMq200Renderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const Board b = bd->GetBoard();
        return b == Board::SimpadSl4 || b == Board::SmartBookG138;
    }

    bool HasFrame() override {
        auto& mq = emu_.Get<MediaQMq200>();
        if (!mq.IsEnabled())  return false;
        if (latch_.Latched()) return true;
        const uint32_t gh = mq.GetGuestH(), stride = mq.Stride();
        if (gh == 0 || stride == 0) return false;
        const uint64_t base  = mq.FbWindowOffset();
        const uint64_t bytes = static_cast<uint64_t>(stride) * gh;
        if (base + bytes > mq.FbSize()) return false;
        return latch_.ProbeAndLatch(mq.FbBytes() + base,
                                    static_cast<size_t>(bytes),
                                    kContentProbeStride);
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib, 0, static_cast<size_t>(host_w) * host_h * 4u);

        auto& mq = emu_.Get<MediaQMq200>();
        const uint32_t gw = mq.GetGuestW(), gh = mq.GetGuestH();
        const uint32_t bpp = mq.Bpp(), stride = mq.Stride();
        if (gw == 0 || gh == 0 || stride == 0 || bpp == 0) return;

        const uint8_t* fb   = mq.FbBytes();
        const uint32_t base = mq.FbWindowOffset();
        if (static_cast<uint64_t>(base) + static_cast<uint64_t>(stride) * gh
                > mq.FbSize())
            return;

        const uint32_t cw = (host_w < gw) ? host_w : gw;
        const uint32_t ch = (host_h < gh) ? host_h : gh;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* line = fb + base + static_cast<size_t>(y) * stride;
            uint32_t* dst = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x)
                dst[x] = DecodePixel(mq, line, x, bpp);
        }
    }

private:
    static uint32_t Expand5(uint32_t v) { return (v << 3) | (v >> 2); }
    static uint32_t Expand6(uint32_t v) { return (v << 2) | (v >> 4); }
    static uint32_t Pack(uint32_t r, uint32_t g, uint32_t b) {
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    static uint32_t FromPalette(MediaQMq200& mq, uint32_t index) {
        const uint32_t e = mq.PaletteEntry(index);   /* R[7:0] G[15:8] B[23:16] (Table 5-32). */
        return Pack(e & 0xFFu, (e >> 8) & 0xFFu, (e >> 16) & 0xFFu);
    }

    static uint32_t DecodePixel(MediaQMq200& mq, const uint8_t* line,
                                uint32_t x, uint32_t bpp) {
        if (bpp == 16u) {
            uint16_t p;
            std::memcpy(&p, line + static_cast<size_t>(x) * 2u, sizeof(p));
            return Pack(Expand5((p >> 11) & 0x1Fu), Expand6((p >> 5) & 0x3Fu),
                        Expand5(p & 0x1Fu));
        }
        if (bpp == 32u) {
            uint32_t p;
            std::memcpy(&p, line + static_cast<size_t>(x) * 4u, sizeof(p));
            return Pack((p >> 16) & 0xFFu, (p >> 8) & 0xFFu, p & 0xFFu);
        }
        if (bpp == 24u) {
            const uint8_t* px = line + static_cast<size_t>(x) * 3u;
            return Pack(px[2], px[1], px[0]);
        }
        if (bpp == 8u) return FromPalette(mq, line[x]);
        /* 1/2/4 bpp palette-indexed, pixel 0 in the byte MSB (CE framebuffer
           convention); not exercised by the SIMpad stock 8/16-bpp modes. */
        const uint32_t ppb   = 8u / bpp;
        const uint32_t mask  = (1u << bpp) - 1u;
        const uint8_t  byte  = line[x / ppb];
        const uint32_t shift = (ppb - 1u - (x % ppb)) * bpp;
        return FromPalette(mq, (byte >> shift) & mask);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(MediaQMq200Renderer, FrameRenderer);
