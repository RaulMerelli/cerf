#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class EmulatedMemory;

/* Re-probing every frame would un-latch when CE clears the fb to
   black, blinking the host window between fb and UART screen.
   One-way latch is intentional. */
class LcdContentLatch {
public:
    bool ProbeAndLatch(EmulatedMemory& mem, uint32_t fb_pa,
                       size_t fb_bytes, size_t stride);

    bool Latched() const {
        return latched_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> latched_{false};
};
