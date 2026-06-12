#include "serial_pccard.h"

#include "serial_endpoint.h"
#include "modem_personality.h"
#include "host_serial_forward.h"

#include "../pcmcia/pcmcia_slot.h"
#include "../../core/cerf_emulator.h"
#include "../../state/emulation_freeze.h"
#include "../../state/state_stream.h"

namespace {

/* Config-register bits (WinCE6 DDK cardserv.h). */
constexpr uint8_t FCR_COR_SRESET = 0x80;
constexpr uint8_t FCR_FCSR_INTR  = 0x02;   /* the bus-driver shared-ISR check bit */

}  /* namespace */

SerialPcCard::SerialPcCard(CerfEmulator& emu) : PcmciaCard(emu) { BuildCis(); }
SerialPcCard::SerialPcCard(CerfEmulator& emu, std::wstring host_com_port)
    : PcmciaCard(emu), mode_(Mode::HostForward),
      host_port_(std::move(host_com_port)) { BuildCis(); }
SerialPcCard::~SerialPcCard() = default;

std::wstring SerialPcCard::TooltipDetail() const {
    if (mode_ == Mode::HostForward)
        return std::wstring(kForwardDisplayName) + L" → host " + host_port_;
    return kDisplayName;
}

/* CIS verified vs pcmcia.dll ParseCfTable + ser_card.c FindComConfig (COR attr
   0x200, cfg idx 1, I/O 0x3F8). FUNCE present => CE6 DetectModem binds Unimodem
   (DeviceType=3); forward mode omits it for a plain serial COMx on CE6 (CE3
   DetectModem hardcodes "Modem" either way - serial.dll!DetectModem 0xF95F98). */
void SerialPcCard::BuildCis() {
    cis_ = {0x21, 0x02, 0x02, 0x00};                  /* CISTPL_FUNCID: serial(2) */
    if (mode_ == Mode::Modem) {
        cis_.insert(cis_.end(), {0x22, 0x01, 0x02});  /* CISTPL_FUNCE: data modem */
        cis_.insert(cis_.end(), {                     /* CISTPL_VERS_1 "Virtual Modem" */
            0x15, 0x16, 0x04, 0x01,
                'C', 'E', 'R', 'F', 0x00,
                'V', 'i', 'r', 't', 'u', 'a', 'l', ' ', 'M', 'o', 'd', 'e', 'm', 0x00,
                0xFF,
        });
    } else {
        cis_.insert(cis_.end(), {                     /* CISTPL_VERS_1 "Serial Forwarder" */
            0x15, 0x19, 0x04, 0x01,
                'C', 'E', 'R', 'F', 0x00,
                'S', 'e', 'r', 'i', 'a', 'l', ' ', 'F', 'o', 'r', 'w', 'a', 'r', 'd', 'e', 'r', 0x00,
                0xFF,
        });
    }
    cis_.insert(cis_.end(), {
        0x1A, 0x05, 0x01, 0x01, 0x00, 0x02, 0x03,     /* CISTPL_CONFIG        */
        0x1B, 0x08, 0xC1, 0x01, 0x08, 0xAA, 0x60,
            0xF8, 0x03, 0x07,                         /* CISTPL_CFTABLE_ENTRY */
        0xFF,                                         /* CISTPL_END           */
    });
}

void SerialPcCard::OnInserted() {
    if (mode_ == Mode::HostForward)
        endpoint_ = std::make_unique<HostSerialForward>(
            host_port_, emu_.Get<EmulationFreeze>());
    else
        endpoint_ = std::make_unique<ModemPersonality>(emu_);
    uart_ = std::make_unique<Serial16550>(*endpoint_,
                                          [this](bool a) { OnUartIrq(a); });
    endpoint_->BindUart(*uart_);
}

void SerialPcCard::OnShutdown() {
    endpoint_.reset();   /* terminator dtor removes the RX callback (barrier) */
    uart_.reset();       /* before the UART the terminator's RX path touches */
}

void SerialPcCard::PowerOn () { uart_->Reset(); endpoint_->OnOpen(); }
void SerialPcCard::PowerOff() { endpoint_->OnClose(); }

void SerialPcCard::OnUartIrq(bool asserted) {
    uart_irq_ = asserted;
    if (asserted) { fcsr_ |= FCR_FCSR_INTR; slot_->RaiseIrq(); }
    else          { fcsr_ &= (uint8_t)~FCR_FCSR_INTR; slot_->ClearIrq(); }
}

uint8_t SerialPcCard::ReadAttribute8(uint32_t offset) {
    /* CIS in attribute memory, 8-bit aliased across even bytes (tuple byte n at
       offset 2n), then the config registers. */
    uint8_t v = 0xFFu;
    if (offset < cis_.size() * 2u)   v = cis_[offset / 2u];
    else if (offset == kCorOffset)   v = cor_;
    else if (offset == kFcsrOffset)  v = fcsr_;
    return v;
}

void SerialPcCard::WriteAttribute8(uint32_t offset, uint8_t value) {
    if (offset == kCorOffset) {
        cor_ = value;
        if (value & FCR_COR_SRESET) uart_->Reset();
        return;
    }
    if (offset == kFcsrOffset) {
        /* Host writes the status config; the INTR bit stays owned by the UART. */
        fcsr_ = (uint8_t)((value & (uint8_t)~FCR_FCSR_INTR) |
                          (uart_irq_ ? FCR_FCSR_INTR : 0u));
        return;
    }
    /* CIS is ROM; ignore other writes. */
}

uint8_t  SerialPcCard::ReadCommon8  (uint32_t)            { return 0xFFu;   }
uint16_t SerialPcCard::ReadCommon16 (uint32_t)            { return 0xFFFFu; }
void     SerialPcCard::WriteCommon8 (uint32_t, uint8_t)   {}
void     SerialPcCard::WriteCommon16(uint32_t, uint16_t)  {}

/* serial.dll reaches the 16550 through the mapped I/O window at the configured
   base (0x3F8), as 8-bit READ/WRITE_PORT_UCHAR accesses to registers 0..7. */
uint8_t SerialPcCard::ReadIo8(uint32_t offset) {
    if (offset - kIoBase < kRegCount) return uart_->ReadReg8(offset - kIoBase);
    return 0xFFu;
}
void SerialPcCard::WriteIo8(uint32_t offset, uint8_t value) {
    if (offset - kIoBase < kRegCount) uart_->WriteReg8(offset - kIoBase, value);
}
uint16_t SerialPcCard::ReadIo16(uint32_t offset) {
    return (uint16_t)(ReadIo8(offset) | (ReadIo8(offset + 1u) << 8));
}
/* mode_/host_port_/cis_/endpoint_ are identity / host binding; the PCMCIA
   config regs + the 16550 UART register file are the card state. uart_ is
   built in OnInserted so it always exists while the card sits in a slot. */
void SerialPcCard::SaveState(StateWriter& w) {
    w.Write(cor_); w.Write(fcsr_);
    w.Write<uint8_t>(uart_irq_ ? 1u : 0u);
    uart_->SaveState(w);
}

void SerialPcCard::RestoreState(StateReader& r) {
    r.Read(cor_); r.Read(fcsr_);
    uint8_t irq = 0; r.Read(irq); uart_irq_ = (irq != 0);
    uart_->RestoreState(r);
}

void SerialPcCard::WriteIo16(uint32_t offset, uint16_t value) {
    WriteIo8(offset, (uint8_t)(value & 0xFFu));
    WriteIo8(offset + 1u, (uint8_t)(value >> 8));
}
