#pragma once

#include "../peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../tracing/kernel_debug_sink.h"
#include "../../state/state_stream.h"
#include "../peripheral_dispatcher.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

/* Generic 16550 UART core. Register offsets are reg-index*RegStride (PXA255 =4,
   off-chip 16-bit-bus TL16C550 =2) — hardcoding word offsets here breaks the
   x2-stride part. */
class Uart16550 : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioSize() const override { return 0x00001000u; }

    uint32_t ReadWord(uint32_t addr) override {
        switch ((addr - MmioBase()) / RegStride()) {
        case 0:                                        /* RBR / DLL. */
            if (dlab()) return dll_;
            {
                uint8_t b;
                {
                    std::lock_guard<std::mutex> lk(rx_mtx_);
                    if (rx_fifo_.empty()) return 0u;   /* empty RX FIFO reads 0. */
                    b = rx_fifo_.front();
                    rx_fifo_.pop_front();
                    rx_size_.store(rx_fifo_.size(), std::memory_order_release);
                }
                RecomputeInterrupt();                  /* DR may clear, RDA may deassert. */
                return b;
            }
        case 1: return dlab() ? dlh_ : ier_;          /* IER / DLH. */
        case 2: {                                      /* IIR (read), PXA255 Table 10-9. */
            uint32_t iir = 0x01u;                      /* IP=1 (bit0): no interrupt pending. */
            if (RxIntPending()) {                       /* RDA: priority over TX (Table 10-8). */
                iir = 0x04u;
            } else if (TxIntPending()) {                /* THRE: acked by reading IIR. */
                iir = 0x02u; tx_acked_ = true; RecomputeInterrupt();
            }
            if (fcr_ & 0x01u) iir |= 0xC0u;
            return iir;
        }
        case 3: return lcr_;
        case 4: return mcr_;
        case 5:                                         /* LSR, PXA255 Table 10-13. */
            /* DR (bit0) from the lock-free RX count so the TX-drain LSR poll never
               takes the FIFO lock. */
            return kLsrTxReady |
                   (rx_size_.load(std::memory_order_acquire) ? 0x01u : 0u);
        case 6: return 0u;                             /* MSR: no modem inputs. */
        case 7: return spr_;
        default: return ReadExtReg((addr - MmioBase()) / RegStride());
        }
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch ((addr - MmioBase()) / RegStride()) {
        case 0: {
            if (dlab()) { dll_ = value; return; }
            const uint8_t b = static_cast<uint8_t>(value & 0xFFu);
            if (tx_observer_) tx_observer_(b);   /* board transport may answer a guest command. */
            EmitTxByte(b);
            tx_acked_ = false;       /* drained -> THRE=1 again -> re-assert if enabled. */
            RecomputeInterrupt();
            return;
        }
        case 1:
            if (dlab()) { dlh_ = value; return; }
            ier_ = value;
            tx_acked_ = false;
            RecomputeInterrupt();
            return;
        case 2: fcr_ = value; return;                  /* FCR (write side). */
        case 3: lcr_ = value; return;
        case 4: mcr_ = value; return;
        case 5: case 6: return;                         /* LSR / MSR read-only. */
        case 7: spr_ = value; return;
        default: WriteExtReg((addr - MmioBase()) / RegStride(), value); return;
        }
    }

    /* 16550 registers are byte-wide; CE writes THR / reads LSR with STRB / LDRB.
       At word stride a byte access maps onto the aligned word register; the
       in-between byte lanes are the register's zero upper bytes. */
    uint8_t ReadByte(uint32_t addr) override {
        if ((addr - MmioBase()) % RegStride() != 0u) return 0u;
        return static_cast<uint8_t>(ReadWord(addr) & 0xFFu);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        if ((addr - MmioBase()) % RegStride() != 0u) return;
        WriteWord(addr, value);
    }

    /* Host-side RX injection: push a received byte into the RX FIFO and refresh
       the interrupt line (RDA). The guest driver pops it from RBR. Used by board
       input transports that stream bytes into the UART (the NEC PCO keyboard/touch
       companion feeds reports into the BTUART this way). */
    /* Runs on board input-transport threads (NEC PCO keyboard/touch pacers, the
       battery TX-observer) while the guest JIT thread pops RBR — rx_mtx_ is the
       mutual exclusion both sides take for every rx_fifo_ access. */
    void PushRx(uint8_t byte) {
        {
            std::lock_guard<std::mutex> lk(rx_mtx_);
            if (rx_fifo_.size() >= kRxFifoCap) {
                LOG(SocUart, "%s RX FIFO overflow (cap %zu), dropping 0x%02X\n",
                    Name(), kRxFifoCap, byte);
                return;
            }
            rx_fifo_.push_back(byte);
            rx_size_.store(rx_fifo_.size(), std::memory_order_release);
        }
        RecomputeInterrupt();
    }

    /* Observe each guest-written TX byte (THR). A board transport that must
       answer a guest command synchronously (e.g. the NEC PCO battery request,
       where the guest writes 0x70 to BTUART THR and waits for the reply over RX)
       registers here and pushes its response via PushRx. Unset by default. */
    void SetTxObserver(std::function<void(uint8_t)> cb) { tx_observer_ = std::move(cb); }

    /* tx_line_ is a host-side console accumulator rebuilt as the guest writes,
       not guest state — not serialized. A concrete with extra registers chains
       its own fields after Uart16550::Save/RestoreState. */
    void SaveState(StateWriter& w) override {
        w.Write(ier_); w.Write(fcr_); w.Write(lcr_); w.Write(mcr_);
        w.Write(spr_); w.Write(dll_); w.Write(dlh_); w.Write(tx_acked_);
        w.Write(static_cast<uint32_t>(rx_fifo_.size()));
        for (uint8_t b : rx_fifo_) w.Write(b);
    }
    void RestoreState(StateReader& r) override {
        r.Read(ier_); r.Read(fcr_); r.Read(lcr_); r.Read(mcr_);
        r.Read(spr_); r.Read(dll_); r.Read(dlh_); r.Read(tx_acked_);
        rx_fifo_.clear();
        uint32_t n = 0; r.Read(n);
        for (uint32_t i = 0; i < n; ++i) { uint8_t b = 0; r.Read(b); rx_fifo_.push_back(b); }
        rx_size_.store(rx_fifo_.size(), std::memory_order_release);
    }

protected:
    virtual uint32_t    RegStride() const = 0;
    virtual const char* Name()      const = 0;

    /* Drive (pending=true) or clear the UART interrupt line; the concrete routes
       it to its controller (PXA255 INTC source, board GPIO, …). One physical line
       for the whole UART — TX (THRE) and RX (RDA) share it. */
    virtual void SetInterruptLine(bool pending) = 0;

    /* Unit-enable gate for the UART interrupt. Baseline 16550 is always enabled;
       the PXA255 UART gates the whole unit on UUE (IER.6, Table 10-7). Gates both
       TX and RX interrupts. */
    virtual bool UnitEnabled() const { return true; }

    /* Chip registers beyond the 8 standard ones (e.g. PXA255 ISR at index 8).
       Default: unsupported access halts. */
    virtual uint32_t ReadExtReg(uint32_t idx) {
        HaltUnsupportedAccess("ReadWord", MmioBase() + idx * RegStride(), 0);
    }
    virtual void WriteExtReg(uint32_t idx, uint32_t value) {
        HaltUnsupportedAccess("WriteWord", MmioBase() + idx * RegStride(), value);
    }

    bool dlab() const { return (lcr_ & 0x80u) != 0u; }
    uint32_t ier() const { return ier_; }

private:
    static constexpr uint32_t kLsrTxReady = 0x60u;  /* TEMT|THRE: TX always ready. */
    static constexpr size_t   kRxFifoCap  = 256u;   /* injected-input backstop. */

    bool TxIntPending() const {
        return UnitEnabled() && (ier_ & 0x02u) && !tx_acked_;          /* TIE  (IER.1). */
    }
    bool RxIntPending() const {
        return UnitEnabled() && (ier_ & 0x01u) &&
               rx_size_.load(std::memory_order_acquire) != 0;          /* RAVIE (IER.0). */
    }
    void RecomputeInterrupt() { SetInterruptLine(TxIntPending() || RxIntPending()); }

    void EmitTxByte(uint8_t ch) {
        emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(ch), tx_line_, Name());
    }

    uint32_t ier_ = 0, fcr_ = 0, lcr_ = 0, mcr_ = 0, spr_ = 0, dll_ = 0, dlh_ = 0;
    bool     tx_acked_ = false;
    std::deque<uint8_t> rx_fifo_;             /* guarded by rx_mtx_. */
    std::mutex          rx_mtx_;
    std::atomic<size_t> rx_size_{0};          /* lock-free mirror of rx_fifo_.size(). */
    std::string tx_line_;
    std::function<void(uint8_t)> tx_observer_;
};
