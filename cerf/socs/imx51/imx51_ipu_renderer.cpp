#define NOMINMAX

#include "imx51_ipu_cpmem.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/frame_renderer.h"

#include <cstring>

namespace {

constexpr size_t   kContentProbeStride = 251;
constexpr uint32_t kBppRgb565 = 3u;   /* CPMEM BPP code for 16bpp (ipu-cpmem.c) */

/* 5:6:5 -> BGRA8888 (top bits replicated for clean 8-bit expansion). */
inline uint32_t Expand565(uint16_t px) {
    const uint8_t r5 = (px >> 11) & 0x1Fu;
    const uint8_t g6 = (px >>  5) & 0x3Fu;
    const uint8_t b5 =  px        & 0x1Fu;
    const uint8_t r  = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    const uint8_t g  = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    const uint8_t b  = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16)
                       | (static_cast<uint32_t>(g) << 8) | b;
}

/* i.MX51 IPUv3EX display scanout -> host frame. The active display IDMAC channel
   (DP background ch23 for the synchronous display; sub_C0A5D1B8) carries the
   framebuffer; its CPMEM descriptor gives the base/resolution/format the guest
   programmed, so nothing is hardcoded. The SYNC2 panel is 800x480 RGB565. */
class Imx51IpuRenderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::FordSyncGen2;
    }

    bool HasFrame() override {
        const auto d = ActiveDisplay();
        if (!d.valid) return false;
        if (latch_.Latched()) return true;
        return latch_.ProbeAndLatch(emu_.Get<EmulatedMemory>(), d.eba,
                                    static_cast<size_t>(d.sl) * d.fh,
                                    kContentProbeStride);
    }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  host_w,
                    uint32_t  host_h) override {
        std::memset(dib_bgra32, 0, static_cast<size_t>(host_w) * host_h * 4u);
        const auto d = ActiveDisplay();
        if (!d.valid) return;   /* no display channel programmed yet - not an error */
        if (d.bpp != kBppRgb565) {
            LOG(Caution, "Imx51IpuRenderer: unimplemented scanout format eba=0x%08X "
                "%ux%u sl=%u bpp_code=%u pfs=%u\n", d.eba, d.fw, d.fh, d.sl, d.bpp, d.pfs);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        const uint8_t* src = emu_.Get<EmulatedMemory>().TryTranslate(d.eba);
        if (!src) return;
        const uint32_t cw = (d.fw < host_w) ? d.fw : host_w;
        const uint32_t ch = (d.fh < host_h) ? d.fh : host_h;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint16_t* srow =
                reinterpret_cast<const uint16_t*>(src + static_cast<size_t>(y) * d.sl);
            uint32_t* drow = dib_bgra32 + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x) drow[x] = Expand565(srow[x]);
        }
    }

    std::optional<FbLayout> GetFbLayout() override {
        const auto d = ActiveDisplay();
        if (!d.valid) return std::nullopt;
        const uint32_t bits = (d.bpp == 0u) ? 32u : (d.bpp == 1u) ? 24u
                            : (d.bpp == 3u) ? 16u : 8u;
        return FbLayout{ d.eba, d.sl, bits, d.bpp == kBppRgb565 };
    }

private:
    Imx51IpuChannelDesc ActiveDisplay() {
        auto* cp = emu_.TryGet<Imx51IpuCpmem>();
        if (!cp) return {};
        for (uint32_t ch : {23u, 28u, 27u, 24u}) {
            const auto d = cp->DecodeChannel(ch);
            if (d.valid) return d;
        }
        return {};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Imx51IpuRenderer, FrameRenderer);
