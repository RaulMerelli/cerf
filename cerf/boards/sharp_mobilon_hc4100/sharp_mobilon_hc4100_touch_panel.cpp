#include "sharp_mobilon_hc4100_touch_panel.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/pr31x00/pr31x00_intc.h"

namespace {
/* The SIB codec IRQ reaches INTC Interrupt Status 1 bit 6 (SIBIRQPOSINT),
   delivered as SYSINTR 29: SIB.dll sub_1543DCC calls InterruptInitialize(29,...)
   then tests Status1 (0x10C00100) & 0x40 (bit 6) in its IST. */
constexpr uint32_t kSibIrqPosIntSet = 0;
constexpr uint32_t kSibIrqPosInt    = 1u << 6;
}  /* namespace */

bool SharpMobilonHc4100TouchPanel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
}

void SharpMobilonHc4100TouchPanel::SetPen(bool down, int x, int y) {
    x_.store(x, std::memory_order_relaxed);
    y_.store(y, std::memory_order_relaxed);
    const bool was = down_.exchange(down, std::memory_order_acq_rel);
    if (down && !was) AssertPenIrq();
}

void SharpMobilonHc4100TouchPanel::AssertPenIrq() {
    const uint16_t armed = pen_irq_armed_.load(std::memory_order_acquire);
    if (!armed) return;
    irq_status_.fetch_or(armed, std::memory_order_acq_rel);
    emu_.Get<Pr31x00Intc>().SetPending(kSibIrqPosIntSet, kSibIrqPosInt);
}

void SharpMobilonHc4100TouchPanel::ClearPenIrq(uint16_t mask) {
    irq_status_.fetch_and(static_cast<uint16_t>(~mask), std::memory_order_acq_rel);
}

REGISTER_SERVICE(SharpMobilonHc4100TouchPanel);
