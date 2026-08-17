#define NOMINMAX

#include "siemens_mp377_sm501.h"

#include "../../peripherals/peripheral_base.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/frame_renderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace {

using siemens_mp377::kSm501FbBytes;
using siemens_mp377::Sm501FbOffsetToPa;
using siemens_mp377::kFbWidth;
using siemens_mp377::kFbHeight;
using siemens_mp377::kFbStride;

void BuildRgb565ToXrgbLut(std::array<uint32_t, 65536>& lut) {
    for (uint32_t p = 0; p < 65536u; ++p) {
        const uint32_t r = ((p >> 11) & 0x1Fu) << 3;
        const uint32_t g = ((p >> 5) & 0x3Fu) << 2;
        const uint32_t b = (p & 0x1Fu) << 3;
        lut[p] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

class SiemensMp377Sm501Renderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    void OnReady() override {
        BuildRgb565ToXrgbLut(rgb565_to_xrgb_);
    }

    bool HasFrame() override {
        if (latched_) return true;
        auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
        if (video && video->WasWritten()) {
            latched_ = true;
            return true;
        }
        return false;
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
        if (!video || !dib || host_w == 0 || host_h == 0) return;
        const uint8_t* vram = video->Vram();
        if (!vram) return;

        uint32_t visible_off = video->PanelFbOffset();
        uint32_t visible_pitch = video->PanelPitchBytes();

        /* The first MP377 progress screen is written through the ATU/OAL
           software framebuffer alias, not through fully-programmed SM501
           panel registers.  In that phase there is no reliable panel pitch
           register yet; use the HWI-selected MP377 framebuffer pitch instead
           of carrying the old 640x480 compatibility fallback. */
        if (visible_pitch < 2u || visible_pitch > kSm501FbBytes / 2u) {
            visible_pitch = kFbStride;
        }

        uint32_t visible_width = visible_pitch / 2u;
        if (visible_width == 0u || visible_width > kFbWidth) visible_width = kFbWidth;

        if (visible_off >= kSm501FbBytes || visible_off + visible_width * 2u > kSm501FbBytes) {
            visible_off = 0u;
            visible_pitch = kFbStride;
            visible_width = visible_pitch / 2u;
            if (visible_width == 0u || visible_width > kFbWidth) visible_width = kFbWidth;
        }

        const uint8_t* base = vram + visible_off;
        const uint32_t fb_w = std::min<uint32_t>(visible_width, host_w);
        const uint32_t max_h_by_vram = (visible_off < kSm501FbBytes && visible_pitch)
            ? ((kSm501FbBytes - visible_off) / visible_pitch)
            : 0u;
        const uint32_t fb_h = std::min<uint32_t>(std::min<uint32_t>(kFbHeight, host_h), max_h_by_vram);

        for (uint32_t y = 0; y < fb_h; ++y) {
            const uint8_t* srow = base + static_cast<size_t>(y) * visible_pitch;
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < fb_w; ++x) {
                const uint32_t i = x * 2u;
                const uint16_t p = static_cast<uint16_t>(srow[i] | (srow[i + 1u] << 8));
                drow[x] = rgb565_to_xrgb_[p];
            }
        }
    }

    std::optional<FbLayout> GetFbLayout() override {
        auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
        if (!video) return std::nullopt;
        const uint32_t visible_off = video->PanelFbOffset();
        uint32_t visible_pitch = video->PanelPitchBytes();
        if (visible_pitch < 2u || visible_pitch > kSm501FbBytes / 2u) visible_pitch = kFbStride;
        return FbLayout{ Sm501FbOffsetToPa(visible_off), visible_pitch, 16u, true };
    }

private:
    std::array<uint32_t, 65536> rgb565_to_xrgb_{};
    bool latched_ = false;
};



} // namespace

REGISTER_SERVICE_AS(SiemensMp377Sm501Renderer, FrameRenderer);

