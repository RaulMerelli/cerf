#define NOMINMAX

#include "imx51_ipu_cpmem.h"
#include "imx51_ipu_srm.h"

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
constexpr uint32_t kBpp32     = 0u;   /* CPMEM BPP code for 32bpp (ipu-cpmem.c) */

/* DP_COM_CONF_SYNC (RM Table 42-116). */
constexpr uint32_t kDpFgEn         = 0x1u; /* FG_EN[0]: partial (FG) plane enabled */
constexpr uint32_t kDpComComposite = 0x3u; /* FG_EN|GWSEL: partial plane, GWAM=0 local alpha */
constexpr uint32_t kDpComGlobal    = 0x7u; /* FG_EN|GWSEL|GWAM: partial plane, global alpha */
constexpr uint32_t kFgPfsRgba8888  = 7u;   /* ch27 PFS for the RGBA8888 FG plane (live: s224) */

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

/* DP graphic-window local per-pixel alpha (RM Table 42-116, GWAM=0):
   out = (A*FG + (255-A)*BG)/255 per channel, round-to-nearest. FG/BG/out are
   0xAARRGGBB words (host DIB BGRA byte order); alpha = FG>>24 (live: FG px b3). */
inline uint32_t BlendLocalAlpha(uint32_t fg, uint32_t bg) {
    const uint32_t a  = fg >> 24;
    const uint32_t ia = 255u - a;
    auto ch = [&](uint32_t sh) -> uint32_t {
        return (a * ((fg >> sh) & 0xFFu) + ia * ((bg >> sh) & 0xFFu) + 127u) / 255u;
    };
    return 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

/* i.MX51 IPUv3EX display scanout -> host frame. The DP composites a full
   BACKGROUND plane (ch23, the synchronous display, RGB565) with a partial
   FOREGROUND graphic window (ch27, RGBA8888) per DP_COM_CONF_SYNC. Descriptors
   come from CPMEM; nothing is hardcoded. The SYNC2 panel is 800x480. */
class Imx51IpuRenderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool ShouldRegister() override {
        if (emu_.Get<DeviceConfig>().guest_additions) return false;
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::FordSyncGen2;
    }

    bool HasFrame() override {
        const auto bg = ActiveDisplay();
        if (!bg.valid) return false;
        if (latch_.Latched()) return true;
        auto& mem = emu_.Get<EmulatedMemory>();
        Imx51IpuChannelDesc fg;
        if (FgActive(fg))   /* content lives in the FG buffer; the BG plane can be empty */
            return latch_.ProbeAndLatch(mem, fg.eba1,
                       static_cast<size_t>(fg.fw) * 4u * fg.fh, kContentProbeStride);
        return latch_.ProbeAndLatch(mem, bg.eba,
                   static_cast<size_t>(bg.sl) * bg.fh, kContentProbeStride);
    }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  host_w,
                    uint32_t  host_h) override {
        std::memset(dib_bgra32, 0, static_cast<size_t>(host_w) * host_h * 4u);
        const auto bg = ActiveDisplay();
        if (!bg.valid) return;   /* no display channel programmed yet - not an error */
        if (bg.bpp != kBppRgb565) {
            LOG(Caution, "Imx51IpuRenderer: unimplemented BG scanout format eba=0x%08X "
                "%ux%u sl=%u bpp_code=%u pfs=%u\n", bg.eba, bg.fw, bg.fh, bg.sl, bg.bpp, bg.pfs);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }

        Imx51IpuChannelDesc fg;
        if (!FgActive(fg)) { BlitBg(dib_bgra32, host_w, host_h, bg); return; }

        auto& srm = emu_.Get<Imx51IpuSrm>();
        const uint32_t comconf = srm.DpComConfSync();
        const uint32_t fgpos   = srm.DpFgPosSync();
        const bool common_ok = (fgpos == 0u && fg.bpp == kBpp32 &&
                                fg.pfs == kFgPfsRgba8888 && fg.alu == 0u &&
                                fg.fw == bg.fw && fg.fh == bg.fh);
        if (common_ok && comconf == kDpComComposite) {
            CompositeFgOverBg(dib_bgra32, host_w, host_h, bg, fg);
            return;
        }
        /* GWAV[31:24]==0 == FG totally opaque (RM Table 42-117); non-zero GWAV is a
           partial global-alpha blend whose combine arithmetic the RM does not state
           as an equation - left FATAL until grounded, never guessed. */
        if (common_ok && comconf == kDpComGlobal &&
            (srm.DpGraphWindCtrlSync() >> 24) == 0u) {
            CompositeFgOpaque(dib_bgra32, host_w, host_h, fg);
            return;
        }
        LOG(Caution, "Imx51IpuRenderer: unmodeled DP composition COM_CONF=0x%08X "
            "GWCTRL=0x%08X FG_POS=0x%08X | FG eba1=0x%08X %ux%u bpp=%u pfs=%u alu=%u | BG %ux%u\n",
            comconf, srm.DpGraphWindCtrlSync(), fgpos, fg.eba1, fg.fw, fg.fh,
            fg.bpp, fg.pfs, fg.alu, bg.fw, bg.fh);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
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

    /* True when DP FG composition is on and ch27 is programmed with a content
       buffer; fills `fg` with ch27's descriptor. ch27.valid is EBA0-based and
       false here (the FG buffer is in EBA1), so readiness is judged from
       eba1 + dimensions directly. */
    bool FgActive(Imx51IpuChannelDesc& fg) {
        auto* cp = emu_.TryGet<Imx51IpuCpmem>();
        if (!cp) return false;
        if (!(emu_.Get<Imx51IpuSrm>().DpComConfSync() & kDpFgEn)) return false;
        fg = cp->DecodeChannel(27u);
        return fg.eba1 != 0u && fg.fw > 1u && fg.fh > 1u;
    }

    void BlitBg(uint32_t* dib, uint32_t host_w, uint32_t host_h,
                const Imx51IpuChannelDesc& bg) {
        const uint8_t* src = emu_.Get<EmulatedMemory>().TryTranslate(bg.eba);
        if (!src) return;
        const uint32_t cw = (bg.fw < host_w) ? bg.fw : host_w;
        const uint32_t ch = (bg.fh < host_h) ? bg.fh : host_h;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint16_t* srow =
                reinterpret_cast<const uint16_t*>(src + static_cast<size_t>(y) * bg.sl);
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x) drow[x] = Expand565(srow[x]);
        }
    }

    void CompositeFgOverBg(uint32_t* dib, uint32_t host_w, uint32_t host_h,
                           const Imx51IpuChannelDesc& bg,
                           const Imx51IpuChannelDesc& fg) {
        auto& mem = emu_.Get<EmulatedMemory>();
        const uint8_t* fg_src = mem.TryTranslate(fg.eba1);
        if (!fg_src) return;
        const uint8_t* bg_src = mem.TryTranslate(bg.eba);  /* may be null -> BG is black */
        const uint32_t fg_stride = fg.fw * 4u;             /* packed 32bpp, FG_POS=0 full-frame */
        const uint32_t cw = (fg.fw < host_w) ? fg.fw : host_w;
        const uint32_t ch = (fg.fh < host_h) ? fg.fh : host_h;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint32_t* frow =
                reinterpret_cast<const uint32_t*>(fg_src + static_cast<size_t>(y) * fg_stride);
            const uint16_t* brow = bg_src
                ? reinterpret_cast<const uint16_t*>(bg_src + static_cast<size_t>(y) * bg.sl)
                : nullptr;
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x) {
                const uint32_t bgpx = brow ? Expand565(brow[x]) : 0xFF000000u;
                drow[x] = BlendLocalAlpha(frow[x], bgpx);
            }
        }
    }

    /* Global-alpha opaque overlay (RM Table 42-116 GWAM=1, Table 42-117 GWAV=0):
       the FG graphic window fully replaces the BG; per-pixel FG alpha is not used
       in global mode. FG is packed RGBA8888 at frame origin (FG_POS=0). */
    void CompositeFgOpaque(uint32_t* dib, uint32_t host_w, uint32_t host_h,
                           const Imx51IpuChannelDesc& fg) {
        const uint8_t* fg_src = emu_.Get<EmulatedMemory>().TryTranslate(fg.eba1);
        if (!fg_src) return;
        const uint32_t fg_stride = fg.fw * 4u;
        const uint32_t cw = (fg.fw < host_w) ? fg.fw : host_w;
        const uint32_t ch = (fg.fh < host_h) ? fg.fh : host_h;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint32_t* frow =
                reinterpret_cast<const uint32_t*>(fg_src + static_cast<size_t>(y) * fg_stride);
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x)
                drow[x] = 0xFF000000u | (frow[x] & 0x00FFFFFFu);
        }
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Imx51IpuRenderer, FrameRenderer);
