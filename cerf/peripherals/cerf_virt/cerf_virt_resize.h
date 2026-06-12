#pragma once

#include "../peripheral_base.h"

#include <atomic>
#include <cstdint>

/* Host<->guest resolution-negotiation MMIO channel for guest-additions auto-resize. */
class CerfVirtResize : public Peripheral {
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

    /* UI thread. Publish a target resolution and bump want-gen last so the
       guest never reads a fresh gen with stale W/H/Bpp. */
    void RequestResize(uint32_t w, uint32_t h, uint32_t bpp);

private:
    std::atomic<uint32_t> want_w_{0};
    std::atomic<uint32_t> want_h_{0};
    std::atomic<uint32_t> want_bpp_{0};
    std::atomic<uint32_t> want_gen_{0};

    std::atomic<uint32_t> applied_w_{0};
    std::atomic<uint32_t> applied_h_{0};
    std::atomic<uint32_t> applied_gen_{0};
};
