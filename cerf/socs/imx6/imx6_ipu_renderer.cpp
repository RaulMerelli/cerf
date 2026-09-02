#define NOMINMAX

#include "imx6_ipu_cpmem.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/panel_frame_renderer.h"

#include <algorithm>
#include <cstring>
#include <optional>

#include <windows.h>

namespace {

constexpr uint32_t kBitsRgb565 = 16u;

inline uint32_t Expand565(uint16_t px) {
    const uint8_t r5 = (px >> 11) & 0x1Fu;
    const uint8_t g6 = (px >> 5) & 0x3Fu;
    const uint8_t b5 = px & 0x1Fu;
    const uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    const uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    const uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

class Imx6IpuRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) {
            return false;
        }
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    bool HasFrame() override {
        const auto d = ActiveDisplay();
        if (!d.valid) return false;
        /* The i.MX6 IPU scans out a real frame as soon as IDMAC is configured
           with a valid CPMEM descriptor.  During early WinCE/GWES bring-up that
           frame may legitimately be all black or all white, so the generic
           "nonzero content" latch would hide a correctly enabled display. */
        if (!scanout_latched_) {
            scanout_latched_ = true;
        }
        return true;
    }

    void RenderInto(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib_bgra32, 0, static_cast<size_t>(host_w) * host_h * 4u);
        const auto d = ActiveDisplay();
        if (!d.valid) return;

        const uint8_t* src = emu_.Get<EmulatedMemory>().TryTranslate(d.eba);
        if (!src) return;
        const uint32_t cw = std::min<uint32_t>(d.fw, host_w);
        const uint32_t ch = std::min<uint32_t>(d.fh, host_h);

        if (d.bits_per_pixel == kBitsRgb565) {
            for (uint32_t y = 0; y < ch; ++y) {
                const uint16_t* srow = reinterpret_cast<const uint16_t*>(src + static_cast<size_t>(y) * d.sl);
                uint32_t* drow = dib_bgra32 + static_cast<size_t>(y) * host_w;
                for (uint32_t x = 0; x < cw; ++x)
                    drow[x] = Expand565(srow[x]);
            }
        } else if (d.bits_per_pixel == 32u) {
            for (uint32_t y = 0; y < ch; ++y) {
                const uint8_t* srow = src + static_cast<size_t>(y) * d.sl;
                uint32_t* drow = dib_bgra32 + static_cast<size_t>(y) * host_w;
                std::memcpy(drow, srow, static_cast<size_t>(cw) * 4u);
            }
        }
    }

    std::optional<FbLayout> GetFbLayout() override {
        const auto d = ActiveDisplay();
        if (!d.valid) return std::nullopt;
        return FbLayout{d.eba, d.sl, d.bits_per_pixel, d.bits_per_pixel == kBitsRgb565};
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        const auto d = ActiveDisplay();
        w = d.valid ? d.fw : 0u;
        h = d.valid ? d.fh : 0u;
    }

private:
    Imx6IpuChannelDesc ActiveDisplay() {
        auto* cp = emu_.TryGet<Imx6IpuCpmem>();
        if (!cp) return {};
        const uint32_t ch = cp->ActiveDisplayChannel();
        if (ch < 64u) {
            return cp->DecodeChannel(ch);
        }
        return {};
    }

    bool scanout_latched_ = false;
};

} /* namespace */

REGISTER_SERVICE_AS(Imx6IpuRenderer, PanelFrameRenderer);
