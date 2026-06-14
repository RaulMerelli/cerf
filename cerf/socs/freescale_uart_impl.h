#pragma once

#include "../peripherals/peripheral_base.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../boards/board_detector.h"
#include "../tracing/kernel_debug_sink.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "../state/state_stream.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace cerf_freescale_uart_detail {

/* Freescale i.MX UART, register-identical on i.MX31 (MCIMX31RM Ch 31) and i.MX51
   (MCIMX51RM Ch 59) — same map + reset values — so the model is shared, gated per
   concrete by kSoc. Status regs MUST read fixed idle: if UTS.TXFULL ever reads set
   or USR1.TRDY clear, the guest TX spin never exits. */
template <uint32_t kBase, int kUartNum, SocFamily kSoc>
class FreescaleUartBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == kSoc;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t a) override {
        const uint32_t off = a - kBase;
        return static_cast<uint8_t>(Reg(off & ~3u) >> ((off & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t a) override {
        const uint32_t off = a - kBase;
        return static_cast<uint16_t>(Reg(off & ~3u) >> ((off & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t a) override { return Reg(a - kBase); }

    void WriteByte(uint32_t a, uint8_t  v) override { Wr(a - kBase, v, (a & 3u) * 8u, 0xFFu); }
    void WriteHalf(uint32_t a, uint16_t v) override { Wr(a - kBase, v, (a & 2u) * 8u, 0xFFFFu); }
    void WriteWord(uint32_t a, uint32_t v) override { Wr(a - kBase, v, 0u, 0xFFFFFFFFu); }

    /* Only the control/baud register file is machine state. tx_line_ is a
       host-side console line accumulator (rebuilt as the guest writes UTXD),
       so it is skipped. */
    void SaveState(StateWriter& w) override    { w.WriteBytes(ctrl_, sizeof(ctrl_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(ctrl_, sizeof(ctrl_)); }

private:
    static constexpr uint32_t kURXD = 0x00u, kUTXD = 0x40u;
    static constexpr uint32_t kUCR2 = 0x84u;
    static constexpr uint32_t kUSR1 = 0x94u, kUSR2 = 0x98u;
    static constexpr uint32_t kONEMS = 0xB0u, kUTS = 0xB4u;
    static constexpr uint32_t kCtrlLo = 0x80u, kCtrlHi = 0xB8u;

    /* SRST self-deasserts after a 4-cycle reset (MCIMX51RM §59.3.3.4); the reset is
       instant in emulation so UCR2 bit0 reads 1 always. Without this, a guest that
       writes SRST=0 then polls bit0 for completion (SBOOT UART3 init) spins forever. */
    static constexpr uint32_t kSrst = 0x0001u;

    /* ONEMS (0xB0) is 24-bit only on i.MX51 (MCIMX51RM §59.3.1); on i.MX31 every
       UART register including ONEMS is 16 LSB (MCIMX31RM §31.3.2). */
    static constexpr uint32_t kOnemsMask =
        (kSoc == SocFamily::iMX51) ? 0xFFFFFFu : 0xFFFFu;

    /* §59.3.3 reset values that ARE the idle TX-ready/RX-empty status: USR1.TRDY
       (0x2040), USR2.TXDC+TXFE (0x4028), UTS.TXEMPTY (0x60, TXFULL clear). */
    static constexpr uint32_t kUsr1Idle = 0x2040u;
    static constexpr uint32_t kUsr2Idle = 0x4028u;
    static constexpr uint32_t kUtsIdle  = 0x0060u;

    /* Control/baud regs 0x80..0xB8 step 4, reset values (MCIMX51RM Table 59-3 /
       MCIMX31RM Table 31-2). ONEMS (0xB0) is the only 24-bit register. */
    uint32_t ctrl_[15] = {
        0x0000u, 0x0001u, 0x0700u, 0x8000u, 0x0801u,  /* UCR1 UCR2 UCR3 UCR4 UFCR */
        0x2040u, 0x4028u, 0x002Bu, 0x0000u, 0x0000u,  /* USR1 USR2 UESC UTIM UBIR */
        0x0000u, 0x0004u, 0x0000u, 0x0060u, 0x0000u,  /* UBMR UBRC ONEMS UTS  UMCR */
    };
    std::string tx_line_;

    uint32_t Reg(uint32_t off) {
        if (off == kURXD) return 0u;        /* no RX data (CHARRDY=0) */
        if (off == kUTXD) return 0u;
        if (off == kUSR1) return kUsr1Idle;
        if (off == kUSR2) return kUsr2Idle;
        if (off == kUTS)  return kUtsIdle;
        if (off >= kCtrlLo && off <= kCtrlHi && (off & 3u) == 0u) {
            uint32_t v = ctrl_[(off - kCtrlLo) / 4u];
            if (off == kUCR2) v |= kSrst;   /* SRST self-deasserts (reset instant) */
            return v;
        }
        HaltUnsupportedAccess("Read", kBase + off, 0);
    }

    void Wr(uint32_t off, uint32_t v, uint32_t shift, uint32_t vmask) {
        const uint32_t aligned = off & ~3u;
        if (aligned == kUTXD) { EmitTx(static_cast<uint8_t>(v & 0xFFu)); return; }
        if (aligned == kUSR1 || aligned == kUSR2) return;  /* W1C status; idle */
        if (aligned >= kCtrlLo && aligned <= kCtrlHi) {
            /* Each register holds only its significant low bits; the upper bits of
               a 32-bit write are "not taken into account" (MCIMX51RM §59.3.1), so
               clamp the merge mask to the register's width. */
            const uint32_t regmax = (aligned == kONEMS) ? kOnemsMask : 0xFFFFu;
            const uint32_t m = (vmask << shift) & regmax;
            uint32_t& r = ctrl_[(aligned - kCtrlLo) / 4u];
            r = (r & ~m) | ((v << shift) & m);
            return;
        }
        HaltUnsupportedAccess("Write", kBase + off, v);
    }

    void EmitTx(uint8_t ch) {
        char tag[8];
        std::snprintf(tag, sizeof(tag), "UART%d", kUartNum);
        emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(ch), tx_line_, tag);
    }
};

}  /* namespace cerf_freescale_uart_detail */
