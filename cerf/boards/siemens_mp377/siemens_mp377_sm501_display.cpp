#define NOMINMAX

#include "siemens_mp377_sm501.h"

#include "../../peripherals/peripheral_base.h"
#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/frame_renderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace {

using siemens_mp377::kFbPa;
using siemens_mp377::kFbBytes;
using siemens_mp377::kFbWidth;
using siemens_mp377::kFbHeight;
using siemens_mp377::kFbStride;

static const std::array<uint32_t, 65536>& Mp377Rgb565ToXrgbLut() {
    static const std::array<uint32_t, 65536> lut = [] {
        std::array<uint32_t, 65536> t{};
        for (uint32_t p = 0; p < 65536u; ++p) {
            const uint32_t r = ((p >> 11) & 0x1Fu) << 3;
            const uint32_t g = ((p >> 5) & 0x3Fu) << 2;
            const uint32_t b = (p & 0x1Fu) << 3;
            t[p] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        return t;
    }();
    return lut;
}

class SiemensMp377Sm501Renderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    bool HasFrame() override {
        if (latched_) return true;
        if (siemens_mp377::Sm501WasWritten(emu_)) { latched_ = true; return true; }
        return false;
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        const uint8_t* vram = siemens_mp377::Sm501Vram(emu_);
        if (!vram || !dib || host_w == 0 || host_h == 0) return;

        uint32_t visible_off = siemens_mp377::Sm501PanelFbOffset(emu_);
        uint32_t visible_pitch = siemens_mp377::Sm501PanelPitchBytes(emu_);
        if (visible_pitch < kFbStride) visible_pitch = kFbStride;

        if (visible_off >= kFbBytes ||
            visible_off + (kFbHeight - 1u) * visible_pitch + kFbWidth * 2u > kFbBytes) {
            visible_off = 0u;
            visible_pitch = kFbStride;
        }

        const uint8_t* base = vram + visible_off;
        const uint32_t fb_w = std::min<uint32_t>(kFbWidth, host_w);
        const uint32_t fb_h = std::min<uint32_t>(kFbHeight, host_h);

        const auto& lut = Mp377Rgb565ToXrgbLut();
        for (uint32_t y = 0; y < fb_h; ++y) {
            const uint8_t* srow = base + static_cast<size_t>(y) * visible_pitch;
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < fb_w; ++x) {
                const uint32_t i = x * 2u;
                const uint16_t p = static_cast<uint16_t>(srow[i] | (srow[i + 1u] << 8));
                drow[x] = lut[p];
            }
        }
    }

    std::optional<FbLayout> GetFbLayout() override {
        const uint32_t visible_off = siemens_mp377::Sm501PanelFbOffset(emu_);
        const uint32_t visible_pitch = siemens_mp377::Sm501PanelPitchBytes(emu_);
        return FbLayout{ kFbPa + visible_off, visible_pitch, 16u, true };
    }

private:
    bool latched_ = false;
};



} // namespace

REGISTER_SERVICE_AS(SiemensMp377Sm501Renderer, FrameRenderer);
