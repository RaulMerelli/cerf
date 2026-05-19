#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/uart_screen.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../boards/board_detector.h"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace {

constexpr uint32_t kUartSize = 0x00001000u;  /* 4 KB per UART */

/* Register offsets within a bank. */
constexpr uint32_t kOffRhrThr = 0x00;
constexpr uint32_t kOffIerDll = 0x04;
constexpr uint32_t kOffIirFcr = 0x08;
constexpr uint32_t kOffLcr    = 0x0C;
constexpr uint32_t kOffMcr    = 0x10;
constexpr uint32_t kOffLsr    = 0x14;
constexpr uint32_t kOffMsr    = 0x18;
constexpr uint32_t kOffSpr    = 0x1C;
constexpr uint32_t kOffMdr1   = 0x20;
constexpr uint32_t kOffScr    = 0x40;
constexpr uint32_t kOffSsr    = 0x44;
constexpr uint32_t kOffSysc   = 0x54;
constexpr uint32_t kOffSyss   = 0x58;
constexpr uint32_t kOffWer    = 0x5C;

constexpr uint8_t kLsrTxReady      = 0x60u;  /* TX_SR_E | TX_FIFO_E */
constexpr uint8_t kLcrDlabMask     = 0x80u;
constexpr uint8_t kSsrIdleVal      = 0x00u;  /* TX FIFO never full */
constexpr uint8_t kSyssRstDoneVal  = 0x01u;  /* reset always complete */
constexpr uint8_t kSyscRstBit      = 0x02u;
constexpr uint8_t kIirNoIntPending = 0x01u;
constexpr uint8_t kMdr1Uart16      = 0x00u;

class Omap3530UartBank : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetSoc() == SocFamily::OMAP3530;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioSize() const override { return kUartSize; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

protected:
    /* Per-bank log tag used in "UART<N> TX: <line>" output. */
    virtual int LogTag() const = 0;

private:
    uint8_t ReadByteLocked (uint32_t addr, uint32_t off);
    void    WriteByteLocked(uint32_t addr, uint32_t off, uint8_t value);
    void    EmitTxByte(uint8_t ch);

    mutable std::mutex state_mutex_;
    uint8_t     ier_   = 0;
    uint8_t     fcr_   = 0;
    uint8_t     lcr_   = 0;
    uint8_t     mcr_   = 0;
    uint8_t     scr_   = 0;
    uint8_t     msr_   = 0;
    uint8_t     spr_   = 0;
    uint8_t     dll_   = 0;
    uint8_t     dlh_   = 0;
    uint8_t     sysc_  = 0;
    uint8_t     wer_   = 0;
    std::string tx_line_;
};

uint8_t Omap3530UartBank::ReadByteLocked(uint32_t addr, uint32_t off) {
    const bool dlab = (lcr_ & kLcrDlabMask) != 0;
    switch (off) {
    case kOffRhrThr: return dlab ? dll_ : 0u;  /* RHR has no source */
    case kOffIerDll: return dlab ? dlh_ : ier_;
    case kOffIirFcr: return kIirNoIntPending;
    case kOffLcr:    return lcr_;
    case kOffMcr:    return mcr_;
    case kOffLsr:    return kLsrTxReady;
    case kOffMsr:    return msr_;
    case kOffSpr:    return spr_;
    case kOffMdr1:   return kMdr1Uart16;
    case kOffScr:    return scr_;
    case kOffSsr:    return kSsrIdleVal;
    case kOffSysc:   return sysc_;
    case kOffSyss:   return kSyssRstDoneVal;
    case kOffWer:    return wer_;
    }
    HaltUnsupportedAccess("ReadByte", addr, 0);
}

void Omap3530UartBank::WriteByteLocked(uint32_t addr, uint32_t off,
                                       uint8_t value) {
    const bool dlab = (lcr_ & kLcrDlabMask) != 0;
    switch (off) {
    case kOffRhrThr:
        if (dlab) dll_ = value;
        else      EmitTxByte(value);
        return;
    case kOffIerDll:
        if (dlab) dlh_ = value;
        else      ier_ = value;
        return;
    case kOffIirFcr: fcr_ = value; return;
    case kOffLcr:    lcr_ = value; return;
    case kOffMcr:    mcr_ = value; return;
    case kOffLsr:                  return;  /* read-only */
    case kOffMsr:    msr_ = value; return;
    case kOffSpr:    spr_ = value; return;
    case kOffMdr1:
        if (value != kMdr1Uart16) {
            HaltUnsupportedAccess("WriteByte(MDR1 non-UART16)",
                                  addr, value);
        }
        return;
    case kOffScr:    scr_ = value; return;
    case kOffSsr:    return;                /* read-only */
    case kOffSysc:
        sysc_ = value & ~kSyscRstBit;
        return;
    case kOffSyss:   return;                /* read-only */
    case kOffWer:    wer_ = value; return;
    }
    HaltUnsupportedAccess("WriteByte", addr, value);
}

void Omap3530UartBank::EmitTxByte(uint8_t ch) {
    /* Caller holds state_mutex_; LOG / UartScreen calls below do
       not nest back into UART writes. */
    if (ch == '\n') {
        LOG(SocUart, "UART%d TX: %s\n", LogTag(), tx_line_.c_str());
        emu_.Get<UartScreen>().AddLine(tx_line_);
        tx_line_.clear();
        return;
    }
    if (ch == '\r') return;  /* CE emits CRLF — drop CR, flush on LF. */
    if (ch >= 0x20 && ch < 0x7F) {
        tx_line_.push_back(static_cast<char>(ch));
    } else {
        char esc[8];
        std::snprintf(esc, sizeof(esc), "\\x%02X", ch);
        tx_line_.append(esc);
    }
    if (tx_line_.size() >= 256) {
        LOG(SocUart, "UART%d TX (no LF, flushed at 256B): %s\n",
            LogTag(), tx_line_.c_str());
        emu_.Get<UartScreen>().AddLine(tx_line_);
        tx_line_.clear();
    }
}

uint8_t Omap3530UartBank::ReadByte(uint32_t addr) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return ReadByteLocked(addr, addr - MmioBase());
}

void Omap3530UartBank::WriteByte(uint32_t addr, uint8_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    WriteByteLocked(addr, addr - MmioBase(), value);
}

uint32_t Omap3530UartBank::ReadWord(uint32_t addr) {
    /* OMAP UART registers are byte-typed (UINT8 in the BSP struct
       OMAP_UART_REGS), but the OAL occasionally issues word-width
       accesses; return the byte zero-extended. */
    std::lock_guard<std::mutex> lk(state_mutex_);
    return ReadByteLocked(addr, addr - MmioBase());
}

void Omap3530UartBank::WriteWord(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    WriteByteLocked(addr, addr - MmioBase(),
                    static_cast<uint8_t>(value & 0xFFu));
}

class Omap3530Uart1 : public Omap3530UartBank {
public:
    using Omap3530UartBank::Omap3530UartBank;
    uint32_t MmioBase() const override { return 0x4806A000u; }
protected:
    int LogTag() const override { return 1; }
};
class Omap3530Uart2 : public Omap3530UartBank {
public:
    using Omap3530UartBank::Omap3530UartBank;
    uint32_t MmioBase() const override { return 0x4806C000u; }
protected:
    int LogTag() const override { return 2; }
};
class Omap3530Uart3 : public Omap3530UartBank {
public:
    using Omap3530UartBank::Omap3530UartBank;
    uint32_t MmioBase() const override { return 0x49020000u; }
protected:
    int LogTag() const override { return 3; }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Uart1);
REGISTER_SERVICE(Omap3530Uart2);
REGISTER_SERVICE(Omap3530Uart3);
