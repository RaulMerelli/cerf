#define NOMINMAX

#include "ati_ragexl_display.h"

#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/frame_renderer.h"

#include <cstring>

namespace {

constexpr size_t kContentProbeStride = 251;

/* Presents the Rage XL framebuffer at the CRTC-programmed geometry. The Mach64
   stores direct-colour pixels little-endian as B,G,R(,X); 16/15 bpp are 5-6-5 /
   5-5-5 (ati Programmer's Guide pixel formats). */
class AtiRageXlRenderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        return emu_.TryGet<RageXlDisplay>() != nullptr;
    }

    bool HasFrame() override {
        const auto f = emu_.Get<RageXlDisplay>().CurrentFrame();
        if (!f.on || f.bpp == 0 || f.stride == 0 || f.height == 0) return false;
        const size_t bytes = (size_t)f.stride * f.height;
        if ((size_t)f.start + bytes > f.fb_size) return false;
        if (latch_.Latched()) return true;
        return latch_.ProbeAndLatch(f.fb + f.start, bytes, kContentProbeStride);
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib, 0, (size_t)host_w * host_h * 4u);
        const auto f = emu_.Get<RageXlDisplay>().CurrentFrame();
        if (!f.on || f.bpp == 0 || f.stride == 0) return;

        const uint32_t cw = (host_w < f.width)  ? host_w : f.width;
        const uint32_t ch = (host_h < f.height) ? host_h : f.height;
        for (uint32_t y = 0; y < ch; ++y) {
            const size_t line_off = (size_t)f.start + (size_t)y * f.stride;
            if (line_off + (size_t)cw * ((f.bpp + 7u) / 8u) > f.fb_size) break;
            const uint8_t* line = f.fb + line_off;
            uint32_t* dst = dib + (size_t)y * host_w;
            for (uint32_t x = 0; x < cw; ++x) dst[x] = DecodePixel(line, x, f.bpp);
        }
    }

private:
    static uint32_t Pack(uint32_t r, uint32_t g, uint32_t b) {
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    static uint32_t Exp5(uint32_t v) { return (v << 3) | (v >> 2); }
    static uint32_t Exp6(uint32_t v) { return (v << 2) | (v >> 4); }

    static uint32_t DecodePixel(const uint8_t* line, uint32_t x, uint32_t bpp) {
        switch (bpp) {
            case 32: { const uint8_t* p = line + x * 4u; return Pack(p[2], p[1], p[0]); }
            case 24: { const uint8_t* p = line + x * 3u; return Pack(p[2], p[1], p[0]); }
            case 16: {
                const uint16_t p = (uint16_t)(line[x * 2u] | (line[x * 2u + 1u] << 8));
                return Pack(Exp5((p >> 11) & 0x1Fu), Exp6((p >> 5) & 0x3Fu), Exp5(p & 0x1Fu));
            }
            case 15: {
                const uint16_t p = (uint16_t)(line[x * 2u] | (line[x * 2u + 1u] << 8));
                return Pack(Exp5((p >> 10) & 0x1Fu), Exp5((p >> 5) & 0x1Fu), Exp5(p & 0x1Fu));
            }
            default: { const uint8_t g = line[x]; return Pack(g, g, g); }  /* 8bpp: palette not modelled */
        }
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(AtiRageXlRenderer, FrameRenderer);
