#define NOMINMAX

#include "pr31x00_lcd.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/frame_renderer.h"

#include <cstring>

namespace {

constexpr size_t kContentProbeStride = 251;

/* The gray modes vary a pixel's duty cycle over frames so the monochrome panel
   takes on one of 16 shades (§17.3.7). */
inline uint32_t ExpandShade(uint32_t shade) {
    const uint8_t g = static_cast<uint8_t>(shade * 17u);   /* 15 -> 255 */
    return 0xFF000000u | ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;
}

/* Figure 17.2.1: the leftmost pixels of a line are driven by UD3, UD2, UD1, UD0,
   so a byte's most significant bits hold the leftmost pixel. */
inline uint32_t RawPixel(const uint8_t* row, uint32_t x, uint32_t bpp) {
    const uint32_t per_byte = 8u / bpp;
    const uint8_t  byte     = row[x / per_byte];
    const uint32_t slot     = x % per_byte;
    const uint32_t shift    = 8u - bpp * (slot + 1u);
    return (byte >> shift) & ((1u << bpp) - 1u);
}

class Pr31x00LcdRenderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::PR31500 || soc == SocFamily::PR31700;
    }

    bool HasFrame() override {
        auto& lcd = emu_.Get<Pr31x00Lcd>();
        if (!lcd.IsEnabled())  return false;
        if (latch_.Latched())  return true;
        const uint32_t fb_pa   = lcd.GetFbPa();
        const uint32_t guest_w = lcd.GetGuestW();
        const uint32_t guest_h = lcd.GetGuestH();
        if (fb_pa == 0 || guest_w == 0 || guest_h == 0) return false;
        const size_t fb_bytes =
            (size_t)guest_h * (size_t)guest_w * lcd.GetBitsPerPixel() / 8u;
        return latch_.ProbeAndLatch(emu_.Get<EmulatedMemory>(),
                                    fb_pa, fb_bytes, kContentProbeStride);
    }

    void RenderInto(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h) override {
        auto& lcd = emu_.Get<Pr31x00Lcd>();
        const uint32_t fb_pa   = lcd.GetFbPa();
        const uint32_t guest_w = lcd.GetGuestW();
        const uint32_t guest_h = lcd.GetGuestH();
        const uint32_t bpp     = lcd.GetBitsPerPixel();

        std::memset(dib_bgra32, 0, (size_t)host_w * host_h * 4u);

        const uint32_t copy_w = (guest_w < host_w) ? guest_w : host_w;
        const uint32_t copy_h = (guest_h < host_h) ? guest_h : host_h;
        if (copy_w == 0 || copy_h == 0) return;

        const uint8_t* src_base = emu_.Get<EmulatedMemory>().TryTranslate(fb_pa);
        if (!src_base) return;

        const size_t stride = (size_t)guest_w * bpp / 8u;
        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint8_t* src_row = src_base + (size_t)y * stride;
            uint32_t* dst_row = dib_bgra32 + (size_t)y * host_w;
            for (uint32_t x = 0; x < copy_w; ++x) {
                dst_row[x] = ExpandShade(lcd.ShadeFor(RawPixel(src_row, x, bpp)));
            }
        }
    }

    std::optional<FbLayout> GetFbLayout() override {
        auto& lcd = emu_.Get<Pr31x00Lcd>();
        const uint32_t pa = lcd.GetFbPa();
        if (pa == 0) return std::nullopt;
        const uint32_t bpp = lcd.GetBitsPerPixel();
        return FbLayout{ pa, lcd.GetGuestW() * bpp / 8u, bpp, false };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Pr31x00LcdRenderer, FrameRenderer);
