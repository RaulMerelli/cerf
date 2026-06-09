#include "serial_pccard.h"

#include "serial_endpoint.h"
#include "modem_personality.h"

#include "../pcmcia/pcmcia_slot.h"
#include "../../core/cerf_emulator.h"

namespace {

constexpr uint8_t FCR_COR_SRESET = 0x80;
constexpr uint8_t FCR_FCSR_INTR  = 0x02;   /* the bus-driver shared-ISR check bit */

const uint8_t kCisData[] = {
    0x21, 0x02, 0x02, 0x00,                       /* CISTPL_FUNCID: serial(2) */
    /* FUNCE type 0x02 = SERIAL_SERV_DATA (data modem; Linux cistpl.h). CE6
       serial.dll (SER_CARD2 DetectModem) binds a serial card as Modem
       (DeviceType=3) only with a non-zero-type FUNCE here; drop it and the
       card binds as a plain COM port (Serial, DeviceType=0), no modem. */
    0x22, 0x01, 0x02,                             /* CISTPL_FUNCE: data modem */
    0x15, 0x16, 0x04, 0x01,                       /* CISTPL_VERS_1 v4.1       */
        'C', 'E', 'R', 'F', 0x00,
        'V', 'i', 'r', 't', 'u', 'a', 'l', ' ', 'M', 'o', 'd', 'e', 'm', 0x00,
        0xFF,
    0x1A, 0x05, 0x01, 0x01, 0x00, 0x02, 0x03,     /* CISTPL_CONFIG            */
    0x1B, 0x08, 0xC1, 0x01, 0x08, 0xAA, 0x60,
        0xF8, 0x03, 0x07,                         /* CISTPL_CFTABLE_ENTRY     */
    0xFF,                                         /* CISTPL_END               */
};
constexpr size_t kCisSize = sizeof(kCisData);

}  /* namespace */

SerialPcCard::SerialPcCard(CerfEmulator& emu) : PcmciaCard(emu) {}
SerialPcCard::~SerialPcCard() = default;

void SerialPcCard::OnInserted() {
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
    if (offset < kCisSize * 2u)      v = kCisData[offset / 2u];
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
void SerialPcCard::WriteIo16(uint32_t offset, uint16_t value) {
    WriteIo8(offset, (uint8_t)(value & 0xFFu));
    WriteIo8(offset + 1u, (uint8_t)(value >> 8));
}
