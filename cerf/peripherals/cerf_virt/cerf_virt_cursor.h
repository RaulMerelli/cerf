#pragma once

#include "../peripheral_base.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

/* Captured guest cursor shape: 1bpp AND/XOR masks (AND rows [0,cy), XOR rows
   [cy,2cy), MSB-first) plus geometry. visible=false means the guest hid it. */
struct GuestCursorShape {
    bool     visible = false;
    uint32_t cx = 0, cy = 0, xhot = 0, yhot = 0, stride = 0;
    std::vector<uint8_t> bits;
};

/* Guest->host cursor-shape MMIO channel for guest additions. The guest's
   GPE::SetPointerShape kicks here; the JIT thread reads the descriptor through
   the live MMU and stores the shape. HostGuestCursor (UI thread) polls Seq()
   and rebuilds the host HCURSOR. */
class CerfVirtCursor : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    uint32_t Seq() const { return seq_.load(); }
    bool GetShape(GuestCursorShape& out);   /* false until a shape arrives */

private:
    bool ReadBlob(uint32_t va, void* out, uint32_t total);

    std::atomic<uint32_t> desc_va_{0};
    std::atomic<uint32_t> seq_{0};
    std::mutex            shape_mutex_;
    GuestCursorShape      shape_;
    bool                  has_shape_ = false;
};
