#include "philips_velo_1_touch_panel.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/pr31x00/pr31x00_intc.h"

namespace {
/* Pen-down drives the rising-edge SIBIRQ source, Interrupt Status 1 bit 6: serial.dll
   sub_1EB85A4 wakes on Status1 & 0x40 and runs the pen handler sub_1EBACE8. The §8.3.1
   register table mislabels bit 6 as SIBIRQNEGINT (it duplicates the bit-5 name); the
   §8.3.1 descriptions and the guest handler pin bit 6 as the pen-down source. */
constexpr uint32_t kSibIrqPosIntSet = 0;
constexpr uint32_t kSibIrqPosInt    = 1u << 6;
}  // namespace

bool PhilipsVelo1TouchPanel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::PhilipsVelo1;
}

void PhilipsVelo1TouchPanel::SetPen(bool down, int x, int y) {
    x_.store(x, std::memory_order_relaxed);
    y_.store(y, std::memory_order_relaxed);
    const bool was = down_.exchange(down, std::memory_order_acq_rel);
    if (down && !was) AssertPenIrq();
}

void PhilipsVelo1TouchPanel::AssertPenIrq() {
    const uint16_t armed = pen_irq_armed_bits_.load(std::memory_order_acquire);
    if (!armed) return;
    irq_status_.fetch_or(armed, std::memory_order_acq_rel);
    emu_.Get<Pr31x00Intc>().SetPending(kSibIrqPosIntSet, kSibIrqPosInt);
}

void PhilipsVelo1TouchPanel::ClearPenIrq(uint16_t mask) {
    irq_status_.fetch_and(static_cast<uint16_t>(~mask), std::memory_order_acq_rel);
}

REGISTER_SERVICE(PhilipsVelo1TouchPanel);
