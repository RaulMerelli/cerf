#pragma once

#include "../../peripherals/uart16550/uart16550.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../state/state_stream.h"
#include "pxa255_intc.h"

#include <cstdint>

/* PXA255 on-chip 16550 UART (Ch10): word register stride; the TX interrupt is
   gated by UUE (IER.6, Table 10-8) and the ISR at index 8 (offset 0x20) selects
   IrDA/UART mode. FFUART/BTUART/STUART differ only by base + INTC source
   (Table 4-35). */
class Pxa255Uart16550 : public Uart16550 {
public:
    using Uart16550::Uart16550;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::PXA25x;
    }

    void SaveState(StateWriter& w) override {
        Uart16550::SaveState(w);
        w.Write(isr_);
    }
    void RestoreState(StateReader& r) override {
        Uart16550::RestoreState(r);
        r.Read(isr_);
    }

protected:
    uint32_t RegStride() const override { return 4u; }

    bool UnitEnabled() const override { return (ier() & 0x40u) != 0u; }  /* UUE (IER.6). */

    void SetInterruptLine(bool pending) override {
        if (pending) emu_.Get<Pxa255Intc>().AssertSource(IntcBit());
        else         emu_.Get<Pxa255Intc>().DeassertSource(IntcBit());
    }

    uint32_t ReadExtReg(uint32_t idx) override {
        if (idx == 8u) return isr_;
        return Uart16550::ReadExtReg(idx);
    }
    void WriteExtReg(uint32_t idx, uint32_t value) override {
        if (idx == 8u) { isr_ = value; return; }
        Uart16550::WriteExtReg(idx, value);
    }

    virtual uint32_t IntcBit() const = 0;

private:
    uint32_t isr_ = 0;
};
