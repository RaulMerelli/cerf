#include "pr31x00_uart.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../../tracing/kernel_debug_sink.h"

#include <cstdint>

namespace {

constexpr uint32_t kOffCtl1     = 0x00u;   /* $0B0 UARTA, $0C8 UARTB */
constexpr uint32_t kOffCtl2     = 0x04u;   /* write-only */
constexpr uint32_t kOffDmaCtl1  = 0x08u;   /* write-only */
constexpr uint32_t kOffDmaCtl2  = 0x0Cu;   /* write-only */
constexpr uint32_t kOffDmaCount = 0x10u;   /* read-only */
constexpr uint32_t kOffData     = 0x14u;   /* write TXHOLD, read RXHOLD */

/* UART Control 2 (§16.5.2), write-only: Baud Rate = f_UARTCLK / ((BAUDRATE + 1) * 16).
   The field is BAUDRATE[10:0] on the TMPR3912 parts and [9:0] on the TMPR3911, and the
   PR31700 carries the TMPR3912 internal function register map (R3912.H). */
constexpr uint32_t kCtl2BaudMask = 0x000007FFu;

/* UART DMA Control 1 (§16.5.3): DMASTARTVAL[31:2] is the DMA buffer's physical
   address; bits 1-0 reserved. DMA Control 2 (§16.5.4): DMALENGTH[15:0]. */
constexpr uint32_t kDmaCtl1Reserved = 0x00000003u;
constexpr uint32_t kDmaCtl2Reserved = 0xFFFF0000u;

/* Transmit Holding (§16.5.6), write-only: BREAK<8> TXDATA[7:0]. Setting BREAK with
   TXDATA $00 drives a line break until it is cleared. */
constexpr uint32_t kTxBreak        = 1u << 8;
constexpr uint32_t kTxDataMask     = 0x000000FFu;
constexpr uint32_t kTxHoldReserved = 0xFFFFFE00u;

/* UART Control 1 (§16.5.1): UARTON<31> EMPTY<30> PRXHOLDFULL<29> RXHOLDFULL<28>
   are read-only; ENUART<0> and the frame-format bits are R/W. EMPTY and DISTXD<6>
   are the only two bits that reset to 1, and UARTON reads 0 while the module is
   off - nk.exe sub_9F411310 stops the CPU only when it is. */
constexpr uint32_t kCtl1Reset = (1u << 30) | (1u << 6);

constexpr uint32_t kCtl1Uarton   = 1u << 31;
constexpr uint32_t kCtl1Empty    = 1u << 30;
constexpr uint32_t kCtl1Reserved = 0x0FFF0000u;
constexpr uint32_t kCtl1DisTxd   = 1u << 6;
constexpr uint32_t kCtl1EnUart   = 1u << 0;

/* ENDMARX<15> and ENDMATX<14> hand the holding registers to the DMA engine,
   ENDMALOOP<10> and ENDMATEST<11> and TESTMODE<13> drive the IC test paths,
   ENBREAKHALT<12> halts on a received break, and LOOPBACK<4> ties TXD to RXD. */
constexpr uint32_t kCtl1Unmodeled = 0x0000FC10u;

/* PULSEOPT2<9> PULSEOPT1<8> DTINVERT<7> DISTXD<6> TWOSTOP<5> BIT_7<3>
   EVENPARITY<2> ENPARITY<1> ENUART<0> shape the frame the module transmits. */
constexpr uint32_t kCtl1Writable = 0x000003EFu;

}  /* namespace */

bool Pr31x00Uart::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const SocFamily soc = bd->GetSoc();
    return soc == SocFamily::PR31500 || soc == SocFamily::PR31700;
}

void Pr31x00Uart::OnReady() {
    ctl1_ = kCtl1Reset & kCtl1Writable;
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* The transmit holding and shift registers drain within the access that fills them,
   so EMPTY never falls and UARTON tracks ENUART with no shutdown delay. Nothing
   drives RXD, so PRXHOLDFULL and RXHOLDFULL stay clear. */
uint32_t Pr31x00Uart::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case kOffCtl1: {
            uint32_t v = (ctl1_ & kCtl1Writable) | kCtl1Empty;
            if (ctl1_ & kCtl1EnUart) v |= kCtl1Uarton;
            return v;
        }
        /* DMACNT reports the DMA counter (§16.5.5) and RXDATA is valid after UARTRXINT
           or while RXHOLD is asserted (§16.5.7); neither engine is modelled. */
        default: HaltUnsupportedAccess("PR31x00 UART ReadWord", addr, 0);
    }
}

void Pr31x00Uart::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - MmioBase()) {
        case kOffCtl1: WriteCtl1(addr, value); return;

        /* BAUDRATE sets the bit rate on the wire and the register cannot be read
           back; a CERF transfer completes within the access that starts it. */
        case kOffCtl2:
            if (value & ~kCtl2BaudMask) {
                HaltUnsupportedAccess("PR31x00 UART CTL2 reserved", addr, value);
            }
            return;

        /* DMASTARTVAL and DMALENGTH address a buffer the DMA engine only walks while
           ENDMARX or ENDMATX is set, and both halt in CTL1; neither register can be
           read back. */
        case kOffDmaCtl1:
            if (value & kDmaCtl1Reserved) {
                HaltUnsupportedAccess("PR31x00 UART DMA_CTL1 reserved", addr, value);
            }
            return;

        case kOffDmaCtl2:
            if (value & kDmaCtl2Reserved) {
                HaltUnsupportedAccess("PR31x00 UART DMA_CTL2 reserved", addr, value);
            }
            return;

        case kOffData: WriteTxHold(addr, value); return;

        default: HaltUnsupportedAccess("PR31x00 UART WriteWord", addr, value);
    }
}

void Pr31x00Uart::WriteCtl1(uint32_t addr, uint32_t value) {
    if (value & kCtl1Reserved) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 reserved", addr, value);
    }
    if (value & kCtl1Unmodeled) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 unmodeled control", addr, value);
    }
    ctl1_ = value & kCtl1Writable;   /* UARTON, EMPTY, PRXHOLDFULL, RXHOLDFULL are read-only */
}

/* DISTXD disconnects TXD from the pin, so a byte written while it is set never
   reaches the wire. */
void Pr31x00Uart::WriteTxHold(uint32_t addr, uint32_t value) {
    if (value & kTxHoldReserved) {
        HaltUnsupportedAccess("PR31x00 UART TXHOLD reserved", addr, value);
    }
    if (value & kTxBreak) {
        HaltUnsupportedAccess("PR31x00 UART TXHOLD BREAK", addr, value);
    }
    if ((ctl1_ & kCtl1EnUart) == 0u || (ctl1_ & kCtl1DisTxd) != 0u) return;
    emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(value & kTxDataMask),
                                         tx_line_, TxSource());
}

void Pr31x00Uart::SaveState(StateWriter& w) { w.Write(ctl1_); }
void Pr31x00Uart::RestoreState(StateReader& r) { r.Read(ctl1_); }
