#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

/* SA-1110 §11.9 / §11.11: SP1, SP2 and SP3 share the same UART
   register surface (UTCR0..3, UTDR, UTSR0/1). MmioBase is the only
   per-port difference at the silicon level; RX-FIFO and TX-listener
   are software opt-ins (e.g. iPaq MicroP on SP1). */

class Sa11xxUartBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void SetTxListener(std::function<void(uint8_t)> fn) {
        tx_listener_ = std::move(fn);
    }
    void PushRxByte(uint8_t b);

    /* Deliver a whole frame atomically (queue all bytes under one lock, raise the
       IRQ once). A streaming source must use this: a mid-frame FIFO underrun can
       leave the guest ISR's sticky RX status set and storm the interrupt. */
    void PushRxBurst(const uint8_t* data, size_t n);

protected:
    /* "UART1", "UART3", … - used as the log prefix on TX flush. */
    virtual const char* ChannelName() const = 0;

    /* SA-1110 §9.2.1.1 INTC source bit for this serial port. SP1 = 15,
       SP2 = 16, SP3 = 17. Return -1 (default) to skip IRQ assertion
       (silent FIFO mode - only useful for ports the kernel polls). */
    virtual int IntcSourceBit() const { return -1; }

private:
    /* Serializes rx_fifo_ + utcr3_ + INTC source bit. Host UI thread
       (PushRxByte) and JIT thread (PopRxByte / UTSR reads / UTCR3
       writes) both touch this state. */
    mutable std::mutex state_mtx_;

    uint32_t utcr0_ = 0;
    uint32_t utcr1_ = 0;
    uint32_t utcr2_ = 0;
    uint32_t utcr3_ = 0;
    uint32_t utcr4_ = 0;     /* SP2-only IrDA control (§11.10.4) */
    /* UTSR0 bits 1 (RFS), 2 (RID) are the level-source bits the
       Linux ISR (drivers/mfd/ipaq-micro.c micro_serial_isr) checks
       and ACKs via W1C. RFS auto-tracks rx_fifo_ size; RID is a
       sticky pulse set after each PushRxByte and cleared by W1C. */
    uint32_t utsr0_pending_ = 0;
    std::deque<uint8_t> rx_fifo_;
    std::function<void(uint8_t)> tx_listener_;
    std::vector<uint8_t> tx_line_;

    /* IRQ-line state mirror: true iff we currently have INTC bit
       asserted. Held so we don't double-assert / double-deassert. */
    bool intc_asserted_ = false;

    void TxByte(uint8_t b);
    void FlushLine();
    uint8_t PopRxByteLocked();          /* caller holds state_mtx_ */
    uint32_t Utsr1Locked() const;       /* caller holds state_mtx_ */
    uint32_t ComputeUtsr0Locked() const;/* caller holds state_mtx_ */
    void RefreshIrqLocked();            /* caller holds state_mtx_ */

    uint32_t ReadReg(uint32_t off);
    void     WriteReg(uint32_t off, uint32_t value);

    static bool IsKnown(uint32_t off) {
        return off == 0x00 || off == 0x04 || off == 0x08 ||
               off == 0x0C || off == 0x10 || off == 0x14 ||
               off == 0x1C || off == 0x20;
    }
};
