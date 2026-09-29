#pragma once

#include <cstdint>
#include <mutex>

#include "../../core/service.h"

class Tsc2017HostState : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    struct Sample {
        bool down = false;
        uint16_t x = 0x800u;
        uint16_t y = 0x800u;
        uint16_t z1 = 0x000u;
        uint16_t z2 = 0x000u;
    };

    enum class PenIrqMode : uint8_t { Enabled = 0, Disabled = 1, ForcedLow = 2 };

    using IrqChangedCallback = void (*)(void*);

    void SetPen(bool down, uint16_t raw_x, uint16_t raw_y);
    Sample Get();
    bool PenIrqPending();
    void ClearPenIrqPending();
    void SetIrqChangedCallback(IrqChangedCallback cb, void* ctx);
    bool PenIrqLineHigh();
    void SetPenIrqMode(PenIrqMode mode, bool notify);

private:
    void NotifyIrqChanged();

    std::mutex mutex_;
    Sample state_;
    bool penirq_pending_ = false;
    PenIrqMode penirq_mode_ = PenIrqMode::Enabled;
    IrqChangedCallback irq_cb_ = nullptr;
    void* irq_ctx_ = nullptr;
};
