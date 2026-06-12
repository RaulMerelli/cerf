#include "serial_16550.h"

#include "serial_endpoint.h"
#include "../../state/state_stream.h"

#include <utility>

/* Register offsets, bits and the interrupt priorities are the WinCE6 DDK
   hw16550.h definitions (the header serial.dll's ser16550 PDD is built on). */
namespace {
constexpr uint32_t kRbrThrDll = 0;   /* RBR/THR, or DLL when LCR.DLAB */
constexpr uint32_t kIerDlm    = 1;   /* IER,     or DLM when LCR.DLAB */
constexpr uint32_t kIirFcr    = 2;   /* IIR (r) / FCR (w)             */
constexpr uint32_t kLcr       = 3;
constexpr uint32_t kMcr       = 4;
constexpr uint32_t kLsr       = 5;
constexpr uint32_t kMsr       = 6;
constexpr uint32_t kScr       = 7;

constexpr uint8_t IER_RDA = 0x01, IER_THR = 0x02, IER_RLS = 0x04, IER_MS = 0x08;

constexpr uint8_t IIR_NONE = 0x01;
constexpr uint8_t IIR_RLS  = 0x06, IIR_RDA = 0x04, IIR_CTI = 0x0C,
                  IIR_THRE = 0x02, IIR_MS  = 0x00;
constexpr uint8_t IIR_FIFO_BITS = 0xC0;
constexpr uint8_t kNoSource     = 0xFF;   /* sentinel (IIR_MS is 0x00) */

constexpr uint8_t FCR_ENABLE = 0x01, FCR_RCVR_RESET = 0x02;

constexpr uint8_t LCR_DLAB = 0x80;

constexpr uint8_t MCR_DTR = 0x01, MCR_RTS = 0x02, MCR_OUT1 = 0x04,
                  MCR_OUT2 = 0x08, MCR_LOOP = 0x10;

constexpr uint8_t LSR_DR = 0x01, LSR_THRE = 0x20, LSR_TEMT = 0x40;
constexpr uint8_t LSR_RLS_BITS = 0x1E;   /* OE|PE|FE|BI: generate the RLS intr */

constexpr uint8_t MSR_DCTS = 0x01, MSR_DDSR = 0x02, MSR_TERI = 0x04,
                  MSR_DDCD = 0x08;
constexpr uint8_t MSR_CTS = 0x10, MSR_DSR = 0x20, MSR_RI = 0x40, MSR_DCD = 0x80;
constexpr uint8_t MSR_DELTAS = MSR_DCTS | MSR_DDSR | MSR_TERI | MSR_DDCD;

/* FCR bits 7:6 select the RX FIFO trigger; FIFO disabled triggers at 1 byte. */
size_t RxTrigger(uint8_t fcr) {
    if (!(fcr & FCR_ENABLE)) return 1;
    switch (fcr & 0xC0) {
        case 0x00: return 1;
        case 0x40: return 4;
        case 0x80: return 8;
        default:   return 14;
    }
}
}  /* namespace */

Serial16550::Serial16550(SerialEndpoint& endpoint, IrqLineFn irq_line)
    : endpoint_(endpoint), irq_line_(std::move(irq_line)) {
    Reset();
}

void Serial16550::Reset() {
    std::lock_guard<std::mutex> lk(mu_);
    ier_ = fcr_ = lcr_ = mcr_ = lsr_ = msr_ = scr_ = 0;
    dll_ = dlm_ = 0;
    thre_armed_ = false;
    rx_.clear();
    rx_pos_ = 0;
    if (last_irq_level_) { last_irq_level_ = false; irq_line_(false); }
}

uint32_t Serial16550::BaudRate() const {
    std::lock_guard<std::mutex> lk(mu_);
    const uint32_t div = (uint32_t)dll_ | ((uint32_t)dlm_ << 8);
    return div ? (115200u / div) : 115200u;
}

/* Decode baud + framing from the divisor latch and LCR. Bit layout is the DDK
   hw16550.h: SERIAL_DATA_MASK 0x03 (5..8 data bits), SERIAL_STOP_MASK 0x04,
   SERIAL_PARITY_MASK 0x38 (NONE/ODD/EVEN/MARK/SPACE at 0x00/0x08/0x18/0x28/0x38). */
Serial16550::LineConfig Serial16550::GetLineConfigLocked() const {
    LineConfig c;
    const uint32_t div = (uint32_t)dll_ | ((uint32_t)dlm_ << 8);
    c.baud = div ? (115200u / div) : 115200u;
    c.data_bits = (uint8_t)(5u + (lcr_ & 0x03u));
    if (!(lcr_ & 0x08u)) {
        c.parity = LineConfig::Parity::None;        /* parity disabled */
    } else {
        switch (lcr_ & 0x38u) {
            case 0x08u: c.parity = LineConfig::Parity::Odd;   break;
            case 0x18u: c.parity = LineConfig::Parity::Even;  break;
            case 0x28u: c.parity = LineConfig::Parity::Mark;  break;
            default:    c.parity = LineConfig::Parity::Space; break;   /* 0x38 */
        }
    }
    if (!(lcr_ & 0x04u))      c.stop = LineConfig::Stop::One;
    else if (c.data_bits == 5) c.stop = LineConfig::Stop::OnePointFive;
    else                       c.stop = LineConfig::Stop::Two;
    return c;
}

Serial16550::LineConfig Serial16550::GetLineConfig() const {
    std::lock_guard<std::mutex> lk(mu_);
    return GetLineConfigLocked();
}

void Serial16550::SetLineConfigCallback(LineConfigFn cb) {
    std::lock_guard<std::mutex> lk(mu_);
    line_cfg_cb_ = std::move(cb);
}

/* Highest-priority IER-enabled pending source, or kNoSource. */
static uint8_t PendingSource(uint8_t ier, uint8_t lsr, uint8_t fcr,
                             size_t rx_avail, bool thre_armed, uint8_t msr) {
    if ((ier & IER_RLS) && (lsr & LSR_RLS_BITS)) return IIR_RLS;
    if (ier & IER_RDA) {
        if (rx_avail >= RxTrigger(fcr)) return IIR_RDA;
        if (rx_avail > 0)               return IIR_CTI;
    }
    if ((ier & IER_THR) && thre_armed)  return IIR_THRE;
    if ((ier & IER_MS) && (msr & MSR_DELTAS)) return IIR_MS;
    return kNoSource;
}

bool Serial16550::ComputeIrqLevelLocked() const {
    if (!(mcr_ & MCR_OUT2)) return false;   /* OUT2 gates the IRQ to the bus */
    return PendingSource(ier_, lsr_, fcr_, RxAvailLocked(), thre_armed_, msr_)
           != kNoSource;
}

void Serial16550::SettleAndFireIrq() {
    const bool level = ComputeIrqLevelLocked();
    if (level != last_irq_level_) {
        last_irq_level_ = level;
        irq_line_(level);   /* safe under mu_: the slot IRQ path never re-enters
                               this UART, and serializing the call here prevents
                               two threads racing stale levels onto the line. */
    }
}

uint8_t Serial16550::ReadIirLocked() {
    const uint8_t src = PendingSource(ier_, lsr_, fcr_, RxAvailLocked(),
                                      thre_armed_, msr_);
    const uint8_t fifo = (fcr_ & FCR_ENABLE) ? IIR_FIFO_BITS : 0;
    if (src == kNoSource) return IIR_NONE | fifo;
    if (src == IIR_THRE) thre_armed_ = false;   /* IIR read clears THRE source */
    return src | fifo;
}

uint8_t Serial16550::ReadReg8(uint32_t offset) {
    uint8_t result = 0xFF;
    bool drained = false;
    {
    std::lock_guard<std::mutex> lk(mu_);
    switch (offset & 7u) {
        case kRbrThrDll:
            if (lcr_ & LCR_DLAB) { result = dll_; break; }
            if (RxAvailLocked() > 0) {
                result = rx_[rx_pos_++];
                if (rx_pos_ == rx_.size()) { rx_.clear(); rx_pos_ = 0; drained = true; }
            } else {
                result = 0;
            }
            break;
        case kIerDlm: result = (lcr_ & LCR_DLAB) ? dlm_ : ier_; break;
        case kIirFcr: result = ReadIirLocked(); break;
        case kLcr:    result = lcr_; break;
        case kMcr:    result = mcr_; break;
        case kLsr:
            /* TX is drained instantly, so THRE+TEMT always read set. */
            result = (uint8_t)(lsr_ | LSR_THRE | LSR_TEMT);
            if (RxAvailLocked() > 0) result |= LSR_DR;
            lsr_ &= (uint8_t)~LSR_RLS_BITS;     /* reading LSR clears error/break */
            break;
        case kMsr:
            result = msr_;
            msr_ &= (uint8_t)~MSR_DELTAS;       /* reading MSR clears delta bits  */
            break;
        case kScr: result = scr_; break;
    }
    SettleAndFireIrq();
    }
    if (drained && rx_drain_) rx_drain_();   /* off-lock: feeder pushes next */
    return result;
}

void Serial16550::WriteReg8(uint32_t offset, uint8_t value) {
    uint8_t tx_byte = 0;
    bool do_tx = false, loop_tx = false, notify_lines = false, dtr = false,
         rts = false, line_changed = false;
    LineConfig   line_cfg;
    LineConfigFn line_cfg_cb;
    {
        std::lock_guard<std::mutex> lk(mu_);
        switch (offset & 7u) {
            case kRbrThrDll:
                if (lcr_ & LCR_DLAB) { dll_ = value; line_changed = true; break; }
                /* THR write: byte goes out, THR re-empties -> THRE re-arms. */
                thre_armed_ = true;
                tx_byte = value;
                if (mcr_ & MCR_LOOP) loop_tx = true; else do_tx = true;
                break;
            case kIerDlm:
                if (lcr_ & LCR_DLAB) { dlm_ = value; line_changed = true; }
                else                   ier_ = value & 0x0Fu;
                break;
            case kIirFcr:
                fcr_ = value;
                if (value & FCR_RCVR_RESET) { rx_.clear(); rx_pos_ = 0; }
                break;
            case kLcr: lcr_ = value; line_changed = true; break;
            case kMcr: {
                const uint8_t old = mcr_;
                mcr_ = value & 0x1Fu;
                if (mcr_ & MCR_LOOP) {
                    /* Loopback wires MCR outputs to MSR inputs (hw16550.h). */
                    uint8_t lvl = 0;
                    if (mcr_ & MCR_RTS)  lvl |= MSR_CTS;
                    if (mcr_ & MCR_DTR)  lvl |= MSR_DSR;
                    if (mcr_ & MCR_OUT1) lvl |= MSR_RI;
                    if (mcr_ & MCR_OUT2) lvl |= MSR_DCD;
                    const uint8_t diff = (uint8_t)((lvl ^ msr_) & 0xF0u);
                    uint8_t d = msr_ & MSR_DELTAS;
                    if (diff & MSR_CTS) d |= MSR_DCTS;
                    if (diff & MSR_DSR) d |= MSR_DDSR;
                    if (diff & MSR_RI)  d |= MSR_TERI;
                    if (diff & MSR_DCD) d |= MSR_DDCD;
                    msr_ = (uint8_t)(lvl | d);
                }
                if ((old ^ mcr_) & (MCR_DTR | MCR_RTS)) {
                    notify_lines = true;
                    dtr = (mcr_ & MCR_DTR) != 0;
                    rts = (mcr_ & MCR_RTS) != 0;
                }
                break;
            }
            case kLsr: case kMsr: break;   /* read-only */
            case kScr: scr_ = value; break;
        }
        SettleAndFireIrq();
        if (line_changed) { line_cfg = GetLineConfigLocked(); line_cfg_cb = line_cfg_cb_; }
    }
    /* Off-lock: the endpoint may call back into PushRx (re-locks mu_). */
    if (do_tx)        endpoint_.OnGuestTx(&tx_byte, 1);
    if (loop_tx)      PushRx(&tx_byte, 1);
    if (notify_lines) endpoint_.OnControlLines(dtr, rts);
    if (line_cfg_cb)  line_cfg_cb(line_cfg);
}

bool Serial16550::RxEmpty() const {
    std::lock_guard<std::mutex> lk(mu_);
    return RxAvailLocked() == 0;
}

void Serial16550::SetRxDrainCallback(RxDrainFn cb) {
    std::lock_guard<std::mutex> lk(mu_);
    rx_drain_ = std::move(cb);
}

void Serial16550::PushRx(const uint8_t* data, size_t n) {
    if (n == 0) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (rx_pos_ == rx_.size()) { rx_.clear(); rx_pos_ = 0; }
    else if (rx_pos_ > 4096) { rx_.erase(rx_.begin(), rx_.begin() + rx_pos_); rx_pos_ = 0; }
    rx_.insert(rx_.end(), data, data + n);
    SettleAndFireIrq();
}

void Serial16550::SaveState(StateWriter& w) const {
    std::lock_guard<std::mutex> lk(mu_);
    w.Write(ier_); w.Write(fcr_); w.Write(lcr_); w.Write(mcr_);
    w.Write(lsr_); w.Write(msr_); w.Write(dll_); w.Write(dlm_);
    w.Write<uint8_t>(thre_armed_ ? 1u : 0u);
    w.Write<uint64_t>(rx_.size());
    if (!rx_.empty()) w.WriteBytes(rx_.data(), rx_.size());
    w.Write<uint64_t>(rx_pos_);
    w.Write<uint8_t>(last_irq_level_ ? 1u : 0u);
}

void Serial16550::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mu_);
    r.Read(ier_); r.Read(fcr_); r.Read(lcr_); r.Read(mcr_);
    r.Read(lsr_); r.Read(msr_); r.Read(dll_); r.Read(dlm_);
    uint8_t armed = 0; r.Read(armed); thre_armed_ = (armed != 0);
    uint64_t n = 0; r.Read(n);
    rx_.assign(static_cast<size_t>(n), 0u);
    if (n) r.ReadBytes(rx_.data(), static_cast<size_t>(n));
    uint64_t pos = 0; r.Read(pos); rx_pos_ = static_cast<size_t>(pos);
    uint8_t lvl = 0; r.Read(lvl); last_irq_level_ = (lvl != 0);
}

void Serial16550::SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) {
    std::lock_guard<std::mutex> lk(mu_);
    if (mcr_ & MCR_LOOP) return;   /* loopback owns MSR; ignore external inputs */
    const uint8_t lvl = (uint8_t)((cts ? MSR_CTS : 0) | (dsr ? MSR_DSR : 0) |
                                  (ri ? MSR_RI : 0) | (dcd ? MSR_DCD : 0));
    const uint8_t old = msr_ & 0xF0u;
    const uint8_t diff = old ^ lvl;
    uint8_t d = msr_ & MSR_DELTAS;
    if (diff & MSR_CTS) d |= MSR_DCTS;
    if (diff & MSR_DSR) d |= MSR_DDSR;
    if ((old & MSR_RI) && !(lvl & MSR_RI)) d |= MSR_TERI;   /* RI trailing edge */
    if (diff & MSR_DCD) d |= MSR_DDCD;
    msr_ = (uint8_t)(lvl | d);
    SettleAndFireIrq();
}
