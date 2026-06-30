#include "simpad_sl4_touch_panel.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

namespace {
constexpr uint16_t kUcbIeTspx  = 1u << 12;   /* UCB_IE_TSPX pen-detect (ucb1x00.h) */
constexpr uint32_t kUcbIrqGpio = 22;         /* GPIO_UCB1300_IRQ (mach-sa1100/simpad.h) */
}  /* namespace */

bool SimpadSl4TouchPanel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SimpadSl4;
}

void SimpadSl4TouchPanel::SetPen(bool down, int x, int y) {
    x_.store(x, std::memory_order_relaxed);
    y_.store(y, std::memory_order_relaxed);
    const bool was = down_.exchange(down, std::memory_order_acq_rel);
    if (down && !was) AssertPenIrq();   /* pen touch = TSPX falling = IE_FAL edge */
}

void SimpadSl4TouchPanel::AssertPenIrq() {
    if (!pen_irq_armed_.load(std::memory_order_acquire)) return;
    irq_status_.fetch_or(kUcbIeTspx, std::memory_order_acq_rel);
    emu_.Get<Sa11xxGpio>().DriveInputPin(kUcbIrqGpio, true);   /* rising -> SYSINTR 24 */
}

void SimpadSl4TouchPanel::ClearPenIrq(uint16_t mask) {
    const uint16_t prev = irq_status_.fetch_and(static_cast<uint16_t>(~mask),
                                                std::memory_order_acq_rel);
    if ((prev & mask & kUcbIeTspx) != 0)
        emu_.Get<Sa11xxGpio>().DriveInputPin(kUcbIrqGpio, false);   /* ready for next edge */
}

REGISTER_SERVICE(SimpadSl4TouchPanel);
