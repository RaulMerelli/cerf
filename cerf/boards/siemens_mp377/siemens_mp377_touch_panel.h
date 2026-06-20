#pragma once

#include <cstdint>

namespace siemens_mp377 {

void Mp377QueueSmiCommand(uint16_t cmd);
uint32_t Mp377TouchReadSmiSampleWord();
uint32_t Mp377TouchReadPenDetectReg();
void Mp377TouchUpdateHostPointer(int x, int y, bool down);
void Mp377TouchCaptureLost();

/* touch.dll uses SYSINTR 0x1B.  P377 NK static map:
   OALIntrStaticTranslate(27, 0x23).  IrqController uses raw IRQ. */
constexpr int kMp377TouchIrqSource = 0x23;

} // namespace siemens_mp377
