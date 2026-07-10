#include "philips_nino_300_touch_panel.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/pr31x00/pr31x00_intc.h"

namespace {
/* The Nino monitors TSMX: sib.dll sub_18D1ED4 arms NEGINTEN=0x2000 and clears
   INTSTAT with 0x2000, so a pen-down latches UCB INTSTAT bit 13 (ucb1x00.h). */
constexpr uint16_t kUcbIeTsmx = 1u << 13;

/* INTR_SIBIRQPOSINT = Interrupt Status 1 bit 6 (R3912.H:372); the OAL ISR
   nk.exe sub_9F433674 delivers it as SYSINTR 29, which touch.dll's sampler
   sub_1891FDC waits on to burst-poll the pen ADC. Latch once per down edge. */
constexpr uint32_t kSibIrqPosIntSet = 0;
constexpr uint32_t kSibIrqPosInt    = 1u << 6;
}  /* namespace */

bool PhilipsNino300TouchPanel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::PhilipsNino300;
}

void PhilipsNino300TouchPanel::SetPen(bool down, int x, int y) {
    x_.store(x, std::memory_order_relaxed);
    y_.store(y, std::memory_order_relaxed);
    const bool was = down_.exchange(down, std::memory_order_acq_rel);
    if (down && !was) AssertPenIrq();
}

void PhilipsNino300TouchPanel::AssertPenIrq() {
    if (!pen_irq_armed_.load(std::memory_order_acquire)) return;
    irq_status_.fetch_or(kUcbIeTsmx, std::memory_order_acq_rel);
    emu_.Get<Pr31x00Intc>().SetPending(kSibIrqPosIntSet, kSibIrqPosInt);
}

void PhilipsNino300TouchPanel::ClearPenIrq(uint16_t mask) {
    irq_status_.fetch_and(static_cast<uint16_t>(~mask), std::memory_order_acq_rel);
}

REGISTER_SERVICE(PhilipsNino300TouchPanel);
