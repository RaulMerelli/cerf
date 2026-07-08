#pragma once

#include "../../core/service.h"

#include <cstdint>
#include <vector>

class StateWriter;
class StateReader;

/* Shared state for the CERF virtual framebuffer. Resolution comes from
   DeviceConfig (board_configurable_screen_width/height); bpp pinned to
   32 (BGRA, host-native). The regs / mem peripherals and the renderer
   all reach this single service to read state. */

class CerfVirtFramebuffer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Width()    const { return width_;  }
    uint32_t Height()   const { return height_; }
    uint32_t Bpp()      const { return bpp_;    }
    uint32_t Stride()   const { return width_ * (bpp_ >> 3u); }
    uint32_t SizeBytes()const { return height_ * Stride();    }
    bool     HasContent() const { return any_write_; }

    /* Total host-backed region: primary surface + offscreen video-memory tail
       the driver carves DDraw surfaces from. Single source of truth for the
       backing vector, the mem peripheral's MmioSize, the kFbRegMemSizeTotal
       register, and the GPE-cmd blitter bounds. Computed once in OnReady. */
    uint32_t RegionBytes() const { return region_bytes_; }

    uint32_t MemBasePa() const;

    /* Byte span reserved at the region head for the (re-mode-growable) primary.
       The guest video heap starts past this so a re-mode can't overrun it. */
    uint32_t PrimaryReserveBytes() const { return primary_reserve_; }

    uint8_t*       Bytes()       { return bytes_.data(); }
    const uint8_t* Bytes() const { return bytes_.data(); }
    uint32_t       Capacity() const { return uint32_t(bytes_.size()); }

    /* Device colour LUT for an indexed (<=8bpp) surface; entry = 0x00RRGGBB. */
    void     SetPaletteEntry(uint32_t idx, uint32_t rgb) { if (idx < 256u) palette_[idx] = rgb; }
    uint32_t PaletteEntry(uint32_t idx) const { return idx < 256u ? palette_[idx] : 0u; }
    const uint32_t* Palette() const { return palette_; }

    void MarkDirty() { any_write_ = true; }

    /* Guest reset: drop the write-seen edge so HasContent stays false
       until the rebooted guest's driver writes the surface again. */
    void ClearContentEdge() { any_write_ = false; }

    /* UI thread only (renderer reads width_/height_ there). A mode whose
       SizeBytes exceeds the boot-fixed region_bytes_ is rejected, else the
       renderer reads past bytes_. */
    void ApplyGuestMode(uint32_t w, uint32_t h);

    /* Snapshot the guest mode + the full host-backed video region (primary +
       offscreen DDraw surfaces). Not an EmulatedMemory region, so the Ram
       section does not cover it; the FB-mem peripheral delegates here. */
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint32_t ComputeRegionBytes();

    std::vector<uint8_t> bytes_;
    uint32_t palette_[256] = { 0 };
    uint32_t width_       = 800;
    uint32_t height_      = 600;
    uint32_t bpp_         = 32;
    uint32_t region_bytes_= 0;
    uint32_t primary_reserve_ = 0;
    bool     any_write_   = false;
};
