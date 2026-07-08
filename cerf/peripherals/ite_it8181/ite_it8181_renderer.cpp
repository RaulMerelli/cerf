#define NOMINMAX

#include "ite_it8181.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/frame_renderer.h"

#include <cstring>

namespace {

constexpr size_t kContentProbeStride = 251;

/* 2 bpp splash -> grayscale: MSB-first (pixel 0 in bits [7:6]), index 0 = dark. */
class IteIt8181Renderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }

    bool HasFrame() override {
        auto& lcd = emu_.Get<IteIt8181>();
        if (!lcd.IsEnabled())  return false;
        if (latch_.Latched())  return true;
        const uint32_t bytes = lcd.StrideBytes() * lcd.GuestH();
        if (bytes == 0u || bytes > lcd.FbSize()) return false;
        return latch_.ProbeAndLatch(lcd.FbBytes(), bytes, kContentProbeStride);
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib, 0, static_cast<size_t>(host_w) * host_h * 4u);

        auto& lcd = emu_.Get<IteIt8181>();
        const uint32_t gw = lcd.GuestW(), gh = lcd.GuestH();
        const uint32_t stride = lcd.StrideBytes();
        if (gw == 0u || gh == 0u || stride == 0u) return;
        if (static_cast<uint64_t>(stride) * gh > lcd.FbSize()) return;

        const uint8_t* fb = lcd.FbBytes();
        const uint32_t cw = (host_w < gw) ? host_w : gw;
        const uint32_t ch = (host_h < gh) ? host_h : gh;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* line = fb + static_cast<size_t>(y) * stride;
            uint32_t* dst = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x) {
                const uint32_t idx = (line[x >> 2] >> ((3u - (x & 3u)) * 2u)) & 0x3u;
                const uint32_t g = idx * 85u;
                dst[x] = 0xFF000000u | (g << 16) | (g << 8) | g;
            }
        }
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(IteIt8181Renderer, FrameRenderer);
