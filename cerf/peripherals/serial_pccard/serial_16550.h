#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class SerialEndpoint;

/* Faithful PC16550D UART engine (register/bit/IRQ model per the WinCE6 DDK
   hw16550.h) so the in-ROM ser16550 MDD/PDD drives it unmodified; a
   SerialEndpoint personality sits behind it. RX arrives via PushRx from the
   personality's thread, so state is mutex-protected (IRQ callback fires off-lock). */
class Serial16550 {
public:
    /* Level-triggered card IRQ. true -> raise the slot IRQ, false -> clear. */
    using IrqLineFn = std::function<void(bool asserted)>;

    Serial16550(SerialEndpoint& endpoint, IrqLineFn irq_line);

    /* Guest register access at I/O-window byte offset 0..7 (hw16550.h). Runs on
       the JIT thread under the slot bus lock. */
    uint8_t ReadReg8 (uint32_t offset);
    void    WriteReg8(uint32_t offset, uint8_t value);

    /* Personality -> RX. Any thread. Asserts the RX interrupt per IER/trigger. */
    void PushRx(const uint8_t* data, size_t n);

    /* True once the guest has read every queued RX byte. */
    bool RxEmpty() const;

    /* Fired off-lock when a guest read empties the RX queue, so a flow-
       controlled feeder can push the next frame. */
    using RxDrainFn = std::function<void()>;
    void SetRxDrainCallback(RxDrainFn cb);

    /* Personality -> modem inputs (CTS/DSR/RI/DCD line levels). Any thread; a
       changed level sets the matching MSR delta bit and (per IER.MS) an IRQ. */
    void SetModemInputs(bool cts, bool dsr, bool ri, bool dcd);

    uint32_t BaudRate() const;   /* derived from the divisor latch + 115200 base */

    /* Decoded line discipline (baud + framing) from the divisor latch + LCR
       (hw16550.h: data-length mask 0x03, stop mask 0x04, parity mask 0x38), for
       a personality that mirrors the guest's settings onto a real host port. */
    struct LineConfig {
        uint32_t baud      = 115200;
        uint8_t  data_bits = 8;   /* 5..8 (LCR bits 1:0 + 5) */
        enum class Parity { None, Odd, Even, Mark, Space } parity = Parity::None;
        enum class Stop   { One, OnePointFive, Two }        stop   = Stop::One;
    };
    LineConfig GetLineConfig() const;

    /* Fired off-lock when the guest changes baud (DLL/DLM under DLAB) or the LCR
       framing, so a host-port forwarder can re-apply SetCommState live. */
    using LineConfigFn = std::function<void(const LineConfig&)>;
    void SetLineConfigCallback(LineConfigFn cb);

    void Reset();                /* power-on / socket-reset defaults */

private:
    bool       ComputeIrqLevelLocked() const;
    uint8_t    ReadIirLocked();           /* reading IIR clears the THRE source */
    void       SettleAndFireIrq();        /* recompute level, call irq_line_ off-lock */
    LineConfig GetLineConfigLocked() const;

    SerialEndpoint& endpoint_;
    IrqLineFn       irq_line_;
    RxDrainFn       rx_drain_;
    LineConfigFn    line_cfg_cb_;

    mutable std::mutex mu_;

    /* Register file (hw16550.h). LCR.DLAB (bit7) maps offsets 0/1 to DLL/DLM. */
    uint8_t ier_ = 0;
    uint8_t fcr_ = 0;
    uint8_t lcr_ = 0;
    uint8_t mcr_ = 0;
    uint8_t lsr_ = 0;        /* error/break bits; THRE/TEMT/DR derived on read */
    uint8_t msr_ = 0;
    uint8_t scr_ = 0;        /* SC_Open's 0xC6 presence test target (ser_card.c) */
    uint8_t dll_ = 0, dlm_ = 0;

    bool thre_armed_ = false;   /* THRE interrupt pending until next IIR read     */

    /* RX line: a 16-byte visible FIFO is modeled as the head of an unbounded
       queue (bytes still "on the wire") so a fast personality never overruns —
       the driver drains 16 per RDA and we refill from the queue. */
    static constexpr size_t kFifoDepth = 16;
    std::vector<uint8_t> rx_;
    size_t rx_pos_ = 0;
    size_t RxAvailLocked() const { return rx_.size() - rx_pos_; }

    bool last_irq_level_ = false;
};
