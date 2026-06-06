#include "lcd_content_latch.h"

#include "../core/log.h"
#include "../cpu/emulated_memory.h"

bool LcdContentLatch::ProbeAndLatch(EmulatedMemory& mem,
                                    uint32_t fb_pa,
                                    size_t   fb_bytes,
                                    size_t   stride) {
    if (latched_.load(std::memory_order_acquire)) return true;
    if (fb_bytes == 0 || stride == 0)             return false;

    /* TryTranslate — fb_pa may not be inside a declared region yet
       in the early mode-program-before-fb-write window; Translate
       halts on unmapped, TryTranslate returns nullptr so the latch
       just stays false until CE publishes a real fb. */
    const uint8_t* fb = mem.TryTranslate(fb_pa);
    if (!fb) return false;
    return ProbeAndLatch(fb, fb_bytes, stride);
}

bool LcdContentLatch::ProbeAndLatch(const uint8_t* fb, size_t fb_bytes,
                                    size_t stride) {
    if (latched_.load(std::memory_order_acquire)) return true;
    if (!fb || fb_bytes == 0 || stride == 0)      return false;

    for (size_t i = 0; i < fb_bytes; i += stride) {
        if (fb[i] != 0) {
            latched_.store(true, std::memory_order_release);
            LOG(Lcd, "content latch: first non-zero byte at fb+0x%zX "
                "(0x%02X)\n", i, (unsigned)fb[i]);
            return true;
        }
    }
    return false;
}
