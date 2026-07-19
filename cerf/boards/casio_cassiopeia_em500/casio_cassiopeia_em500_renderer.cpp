#define NOMINMAX

#include "casio_cassiopeia_em500_companion.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/frame_renderer.h"

#include <cstring>
#include <optional>

namespace {

constexpr size_t kContentProbeStride = 251;

/* ddi.dll @0xFC5458-0xFC546C (andi 0xF800/0x7E0/0x1F channel split): the 16bpp
   framebuffer is RGB565. */
class CasioCassiopeiaEm500Renderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioCassiopeiaEm500;
    }

    bool HasFrame() override {
        auto& asic = emu_.Get<CasioCassiopeiaEm500Companion>();
        if (!asic.IsDisplayEnabled()) return false;
        if (latch_.Latched())         return true;
        const uint32_t bytes = asic.StrideBytes() * asic.GuestH();
        if (bytes == 0u || bytes > asic.FbSize()) return false;
        return latch_.ProbeAndLatch(asic.FbBytes(), bytes, kContentProbeStride);
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib, 0, static_cast<size_t>(host_w) * host_h * 4u);

        auto& asic = emu_.Get<CasioCassiopeiaEm500Companion>();
        const uint32_t gw = asic.GuestW(), gh = asic.GuestH();
        const uint32_t stride = asic.StrideBytes();
        if (static_cast<uint64_t>(stride) * gh > asic.FbSize()) return;

        const uint8_t* fb = asic.FbBytes();
        const uint32_t cw = (host_w < gw) ? host_w : gw;
        const uint32_t ch = (host_h < gh) ? host_h : gh;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* line = fb + static_cast<size_t>(y) * stride;
            uint32_t* dst = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x) {
                uint16_t px;
                std::memcpy(&px, line + x * 2u, sizeof(px));
                const uint32_t r5 = (px >> 11) & 0x1Fu;
                const uint32_t g6 = (px >> 5)  & 0x3Fu;
                const uint32_t b5 =  px        & 0x1Fu;
                const uint32_t r8 = (r5 << 3) | (r5 >> 2);
                const uint32_t g8 = (g6 << 2) | (g6 >> 4);
                const uint32_t b8 = (b5 << 3) | (b5 >> 2);
                dst[x] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
            }
        }
    }

    std::optional<FbLayout> GetFbLayout() override {
        auto& asic = emu_.Get<CasioCassiopeiaEm500Companion>();
        return FbLayout{asic.FbPa(), asic.StrideBytes(), 16u, true};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioCassiopeiaEm500Renderer, FrameRenderer);
