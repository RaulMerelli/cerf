#pragma once

#include <cstdint>
#include <functional>

#include "../../core/service.h"
#include "../../state/state_stream.h"

/* A board input source on one i.MX6 GPIO bank (self-registers with Imx6GpioBus,
   concrete in the board tree). The bank hands it a Reevaluate hook (its
   UpdateIrq); without calling it, an async /PENIRQ arriving on the host thread
   never re-drives the interrupt and the guest misses the edge. */
class Imx6GpioInputSource : public Service {
public:
    using Service::Service;

    /* GPIO bank MMIO base this source drives (e.g. 0x020B0000 = GPIO6). */
    virtual uint32_t GpioBase() const = 0;

    /* Modify the sampled input-pad levels (board-driven input pins). */
    virtual uint32_t ApplyPadInputs(uint32_t inputs) { return inputs; }
    /* Pending ISR bits to OR into the bank's status (already latched by the
       board — e.g. an edge-triggered /PENIRQ the OAL demuxes). */
    virtual uint32_t PendingIsr() { return 0; }
    /* The guest wrote GPIO_ISR (write-1-clear); let the board clear its latch. */
    virtual void OnIsrClear(uint32_t value) { (void)value; }
    /* Modify a GPIO_DR read (board-driven input transforms, e.g. a bit-bang
       acknowledge line). */
    virtual uint32_t ApplyDataRead(uint32_t dr) { return dr; }

    /* Hibernation: the owning bank forwards these (codec pattern). */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}

    void SetReevaluate(std::function<void()> fn) { reevaluate_ = std::move(fn); }

protected:
    /* Re-drive the owning bank's interrupt after an asynchronous state change. */
    void Reevaluate() {
        if (reevaluate_) reevaluate_();
    }

private:
    std::function<void()> reevaluate_;
};
