#include "siemens_mp377_panel.h"
#include "siemens_mp377_sm501.h"
#include "siemens_mp377_sm501_internal.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/service.h"
#include "../board_context.h"

#include <cstdint>

namespace siemens_mp377 {
namespace {

/* SM501 Databook v1.02 section 5 register map. */
constexpr uint32_t kDcPanelFbAddress = 0x08000Cu;
constexpr uint32_t kDcPanelFbOffset = 0x080010u;
constexpr uint32_t kDcPanelFbWidth = 0x080014u;
constexpr uint32_t kDcPanelHTotal = 0x080024u;
constexpr uint32_t kDcPanelVTotal = 0x08002Cu;

/* Standard mode timings for the three MP377 panel resolutions, in
   references/linux/drivers/video/fbdev/modedb.c, fb_videomode fields
   {left_margin, right_margin, upper_margin, lower_margin, hsync_len,
   vsync_len}.  From modedb[] ("Standard video mode definitions (taken from
   XFree86)"): 800x600@60 {88,40,23,1,128,4} and 1024x768@60
   {168,8,29,3,144,6}.  From vesa_modes[], entry "20 1280x1024-60 VESA":
   1280x1024@60 {248,48,38,1,112,3}. */
struct PanelTiming {
    uint32_t h_total;
    uint32_t v_total;
};

constexpr PanelTiming TimingFor(SiemensMp377PanelProfile p) {
    return p == SiemensMp377PanelProfile::Inch12_800x600    ? PanelTiming{1056u, 628u}
           : p == SiemensMp377PanelProfile::Inch15_1024x768 ? PanelTiming{1344u, 806u}
                                                            : PanelTiming{1688u, 1066u};
}

/* SM501 Databook v1.02 section 5, Panel Horizontal Total (MMIO_base +
   0x080024): HT bits[27:16] is the "panel horizontal total specified as number
   of pixels - 1" and HDE bits[11:0] the "horizontal display end specified as
   number of pixels - 1".  Panel Vertical Total (0x08002C) is the same shape:
   VT bits[26:16] lines - 1, VDE bits[10:0] lines - 1. */
constexpr uint32_t TotalReg(uint32_t total, uint32_t visible) {
    return ((total - 1u) << 16) | (visible - 1u);
}

class SiemensMp377BootloaderSm501 final : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    void OnReady() override {
        auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
        const auto panel = kMp377HwiPanel;
        const auto timing = TimingFor(panel.profile);
        const uint32_t pitch = panel.width * (panel.bpp / 8u);

        /* SM501 Databook v1.02 section 5, Panel FB Offset (0x080010):
           [29:20] FB Window Width and [13:4] FB Offset are both "128-bit
           aligned bytes per line", i.e. bytes/16.  Panel FB Width (0x080014):
           [27:16] FB Global Width is "specified in pixels" and [11:0] WX is
           the starting x-coordinate, also in pixels. */
        const uint32_t units = pitch / 16u;
        Write(regs, kDcPanelFbAddress, 0u);
        Write(regs, kDcPanelFbOffset, (units << 20) | (units << 4));
        Write(regs, kDcPanelFbWidth, panel.width << 16);
        Write(regs, kDcPanelHTotal, TotalReg(timing.h_total, panel.width));
        Write(regs, kDcPanelVTotal, TotalReg(timing.v_total, panel.height));

        LOG(Board,
            "SiemensMp377BootloaderSm501: panel preset %ux%ux%u, "
            "h_total %u v_total %u, pitch %u bytes\n",
            panel.width, panel.height, panel.bpp, timing.h_total, timing.v_total, pitch);
    }

private:
    static void Write(SiemensMp377Sm501Regs& regs, uint32_t off, uint32_t v) {
        regs.WriteWord(kSm501RegsBarPa + off, v);
    }
};

} // namespace

REGISTER_SERVICE(SiemensMp377BootloaderSm501);

} // namespace siemens_mp377
