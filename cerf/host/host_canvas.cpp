#define NOMINMAX

#include "host_canvas.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "emulation_pause.h"
#include "frame_renderer.h"
#include "host_canvas_input.h"
#include "keyboard_router.h"
#include "lcd_scan_tick.h"
#include "memory_visualizer.h"
#include "hw_boot_animation.h"
#include "hw_screen.h"

REGISTER_SERVICE(HostCanvas);

void HostCanvas::CreateOn(HWND parent, const RECT& rect,
                          uint32_t surf_w, uint32_t surf_h) {
    canvas_.SetSource(emu_.TryGet<FrameRenderer>());
    canvas_.CreateOn(parent, rect, surf_w, surf_h);
}

void HostCanvas::Reposition(const RECT& r) {
    canvas_.Reposition(r);
}

void HostCanvas::SetTab(Tab t, bool user_initiated) {
    if (user_initiated) user_picked_view_ = true;
    if (tab_ == t) return;
    if (tab_ == Tab::Framebuffer) emu_.Get<HostCanvasInput>().ReleasePenIfDown();
    else if (tab_ == Tab::MemoryVisualizer) {
        if (GetCapture() == canvas_.Hwnd()) ReleaseCapture();
        if (auto* mv = emu_.TryGet<MemoryVisualizer>()) mv->CancelInput();
    }
    tab_ = t;
    canvas_.SetFramebufferActive(tab_ == Tab::Framebuffer);
    if (canvas_.Hwnd()) InvalidateRect(canvas_.Hwnd(), nullptr, FALSE);
}

void HostCanvas::RearmFramebufferAutoSwitch() {
    /* A guest reboot returns to the Framebuffer when video resumes, so the
       user's prior manual tab pick is stale - clear it too, else once the user
       ever switches tabs the reboot auto-switch is suppressed forever. */
    latched_once_     = false;
    user_picked_view_ = false;
}

void HostCanvas::OnPresentTick() {
    if (auto* tick = emu_.TryGet<LcdScanTick>()) tick->OnHostTick();

    const bool has_frame = canvas_.SourceHasFrame();
    if (has_frame && !latched_once_) {
        latched_once_ = true;
        emu_.Get<HwBootAnimation>().OnFramebufferLatched();
        if (!user_picked_view_) {
            tab_ = Tab::Framebuffer;
            canvas_.SetFramebufferActive(true);
        }
    }

    /* Resume wake nudge: the guest can resume with the panel dimmed-until-input
       (Jornada 820), so the framebuffer never repaints and OnFramebufferLatched
       never fires. While stuck on the Resuming screen, send Tab via the active
       keyboard source every second to undim it; give up + warn after 10s. */
    auto& boot = emu_.Get<HwBootAnimation>();
    const bool stuck = (tab_ == Tab::Hw) && !latched_once_ && boot.IsResuming();
    if (!stuck) {
        resume_nudge_start_ms_ = 0;
        resume_nudge_warned_   = false;
        return;
    }
    const uint32_t now = static_cast<uint32_t>(GetTickCount());
    if (resume_nudge_start_ms_ == 0) {
        resume_nudge_start_ms_ = now;
        resume_nudge_last_ms_  = now;   /* first nudge one interval after going stuck */
    }
    if (now - resume_nudge_start_ms_ >= 10000u) {
        if (!resume_nudge_warned_) {
            resume_nudge_warned_ = true;
            boot.SetResumeStalled();
            LOG(Caution, "resume: framebuffer blank 10s after wake; guest may have "
                         "hung (use View -> Framebuffer + input if only dimmed)\n");
        }
        return;
    }
    if (now - resume_nudge_last_ms_ >= 1000u) {
        resume_nudge_last_ms_ = now;
        auto& kr = emu_.Get<KeyboardRouter>();
        kr.OnHostKey(0x09 /* VK_TAB */, /*key_up=*/false);
        kr.OnHostKey(0x09 /* VK_TAB */, /*key_up=*/true);
    }
}

bool HostCanvas::RenderAltContent(HDC dc, uint32_t* bits, int w, int h) {
    if (tab_ == Tab::Framebuffer) return false;   /* canvas composes the frame */
    if (tab_ == Tab::MemoryVisualizer) {
        if (auto* mv = emu_.TryGet<MemoryVisualizer>())
            mv->RenderInto(dc, bits, (uint32_t)w, (uint32_t)h);
        return true;
    }
    emu_.Get<HwScreen>().RenderInto(dc, bits, (uint32_t)w, (uint32_t)h);
    return true;
}

bool HostCanvas::ShouldDesaturatePresent() {
    return tab_ == Tab::Framebuffer && emu_.Get<EmulationPause>().IsPaused();
}

bool HostCanvas::HandleInput(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                             LRESULT& out) {
    return emu_.Get<HostCanvasInput>().Handle(hwnd, msg, wp, lp, out);
}
