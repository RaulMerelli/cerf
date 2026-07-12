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
    /* IER bits 7:4 are DMAE/UUE/NRZE/RTOIE, "used differently from the standard 16550
       register definition" (Table 10-7): UUE gates the unit ("0 - The unit is disabled")
       and RTOIE separately enables the character-timeout interrupt. The 64-byte FIFOs
       trigger at 1/8/16/32 bytes (Table 10-11, FCR[ITL]). */
    explicit Pxa255Uart16550(CerfEmulator& emu)
        : Uart16550(emu, Config{/*ier_mask=*/0xFFu,
                                /*irq_gate_mcr=*/0u,
                                /*irq_gate_ier=*/0x40u,
                                /*rx_trigger=*/{1u, 8u, 16u, 32u},
                                /*cti_ier_bit=*/0x10u}) {}

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
