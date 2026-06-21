#define NOMINMAX
#include <windows.h>

#include "cerf_virt_framebuffer.h"

#include "cerf_virt_addr_map.h"
#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../state/state_stream.h"

REGISTER_SERVICE(CerfVirtFramebuffer);

bool CerfVirtFramebuffer::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

static const uint32_t kOffscreenMultiple = 3u;

namespace {
struct MaxMonitorDims { uint32_t w = 0; uint32_t h = 0; };

/* EnumDisplayMonitors callback (C function-pointer; LPARAM carries the
   accumulator). Keeps the dims of the largest-by-area single monitor. */
BOOL CALLBACK AccumulateMaxMonitor(HMONITOR, HDC, LPRECT rc, LPARAM lp) {
    auto* m = reinterpret_cast<MaxMonitorDims*>(lp);
    const uint32_t w = (uint32_t)(rc->right - rc->left);
    const uint32_t h = (uint32_t)(rc->bottom - rc->top);
    if ((uint64_t)w * h > (uint64_t)m->w * m->h) { m->w = w; m->h = h; }
    return TRUE;
}
}  /* namespace */

uint32_t CerfVirtFramebuffer::ComputeRegionBytes() {
    /* bytes_ cannot grow after this (guest maps it by PA, MmioSize fixed), and
       the surface tops out at the host monitor it lands on - which may not be
       the primary - so reserve for the largest single monitor on the desktop. */
    const uint32_t bytes_per_px = bpp_ >> 3u;
    uint32_t max_primary = SizeBytes();
    MaxMonitorDims mon;
    EnumDisplayMonitors(nullptr, nullptr, &AccumulateMaxMonitor, (LPARAM)&mon);
    if (mon.w == 0 || mon.h == 0) {
        mon.w = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
        mon.h = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
    }
    const uint32_t mon_primary = mon.w * mon.h * bytes_per_px;
    if (mon_primary > max_primary) max_primary = mon_primary;

    /* The primary occupies the region head and GROWS on re-mode up to this span;
       the driver starts its offscreen video heap past it, so a larger re-mode
       never overruns cached icon/bitmap surfaces (the high-res scanline bug). */
    primary_reserve_ = max_primary;
    uint32_t desired = max_primary * (1u + kOffscreenMultiple);
    if (desired < CerfVirt::kFramebufferMemSize)
        desired = CerfVirt::kFramebufferMemSize;

    const uint32_t window = (CerfVirt::kBaseAddr + CerfVirt::kTotalSize)
                            - CerfVirt::kFramebufferMemBase;
    if (desired > window) {
        LOG(Caution, "[CerfVirtFramebuffer] %ux%u needs %u B FB region, only "
                     "%u B fits in the cerf_virt window; raise kTotalSize in "
                     "cerf_virt_addr_map.h to support this resolution\n",
            width_, height_, desired, window);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    return desired;
}

void CerfVirtFramebuffer::OnReady() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    width_  = cfg.board_configurable_screen_width;
    height_ = cfg.board_configurable_screen_height;
    bpp_    = emu_.Get<BoardDetector>().GetGuestAdditionsColorDepth();
    region_bytes_ = ComputeRegionBytes();
    bytes_.assign(region_bytes_, 0);
    LOG(Periph, "[CerfVirtFramebuffer] %ux%u %ubpp stride=%u "
                "fb_size=%u region=%u bytes (offscreen=%u bytes)\n",
        width_, height_, bpp_, Stride(), SizeBytes(),
        region_bytes_, region_bytes_ - SizeBytes());
}

void CerfVirtFramebuffer::SaveState(StateWriter& w) {
    /* bpp_/region_bytes_/primary_reserve_ are boot-fixed from DeviceConfig +
       host monitors and re-derived in OnReady, so only the live guest mode and
       the pixel region travel in the image. */
    w.Write(width_);
    w.Write(height_);
    w.Write<uint8_t>(any_write_ ? 1u : 0u);
    w.Write<uint64_t>(bytes_.size());
    if (!bytes_.empty()) w.WriteBytes(bytes_.data(), bytes_.size());
}

void CerfVirtFramebuffer::RestoreState(StateReader& r) {
    r.Read(width_);
    r.Read(height_);
    uint8_t aw = 0;
    r.Read(aw);
    any_write_ = (aw != 0);
    uint64_t n = 0;
    r.Read(n);
    if (n == bytes_.size()) {
        if (n) r.ReadBytes(bytes_.data(), bytes_.size());
    } else {
        /* The saved video region was sized for a different host monitor set, so
           it cannot map onto this run's region; consume the bytes to keep the
           peripheral stream aligned and let the guest repaint. */
        LOG(Caution, "[CerfVirtFramebuffer] saved FB region %llu B != live %zu B; "
                     "FB content not restored\n",
            static_cast<unsigned long long>(n), bytes_.size());
        std::vector<uint8_t> discard(static_cast<size_t>(n));
        if (n) r.ReadBytes(discard.data(), static_cast<size_t>(n));
    }
}

void CerfVirtFramebuffer::ApplyGuestMode(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;
    const uint32_t need = h * (w * (bpp_ >> 3u));
    if (need > primary_reserve_) {
        LOG(Caution, "[CerfVirtFramebuffer] guest applied %ux%u (%u B) exceeds "
                     "primary reserve %u B; ignoring\n", w, h, need, primary_reserve_);
        return;
    }
    width_  = w;
    height_ = h;
    LOG(Periph, "[CerfVirtFramebuffer] guest re-moded to %ux%u stride=%u\n",
        width_, height_, Stride());
}
