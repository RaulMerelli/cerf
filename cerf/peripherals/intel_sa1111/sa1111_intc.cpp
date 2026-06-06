#include "sa1111_intc.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../socs/sa1110/sa1110_gpio.h"
#include "../peripheral_dispatcher.h"

bool Sa1111Intc::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Jornada720;
}

void Sa1111Intc::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* Offsets within the 0x40001600 block, Developer's Manual Table 11-2. */
uint32_t Sa1111Intc::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case 0x00: return inttest0_;
        case 0x04: return inttest1_;
        case 0x08: return enable0_;
        case 0x0C: return enable1_;
        case 0x10: return polarity0_;
        case 0x14: return polarity1_;
        case 0x18: return tstsel_;
        case 0x1C: return status0_;     /* INTSTATCLR0 read = pending. */
        case 0x20: return status1_;
        case 0x24: return 0;            /* INTSET0 write-1-to-set; read unspecified. */
        case 0x28: return 0;            /* INTSET1. */
        case 0x2C: return wake_en0_;
        case 0x30: return wake_en1_;
        case 0x34: return wake_pol0_;
        case 0x38: return wake_pol1_;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Sa1111Intc::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - MmioBase()) {
        case 0x00: inttest0_  = value; return;
        case 0x04: inttest1_  = value; return;
        case 0x08: enable0_   = value; DriveCascadeOutput(false); return;
        case 0x0C: enable1_   = value; DriveCascadeOutput(false); return;
        case 0x10: polarity0_ = value; return;
        case 0x14: polarity1_ = value; return;
        case 0x18: tstsel_    = value; return;
        case 0x1C: status0_  &= ~value;           /* INTSTATCLR0 W1C. */
                   DriveCascadeOutput(true); return;
        case 0x20: status1_  &= ~value;
                   DriveCascadeOutput(true); return;
        case 0x24: status0_  |= value;            /* INTSET0 software-set. */
                   DriveCascadeOutput(false); return;
        case 0x28: status1_  |= value;
                   DriveCascadeOutput(false); return;
        case 0x2C: wake_en0_  = value; return;
        case 0x30: wake_en1_  = value; return;
        case 0x34: wake_pol0_ = value; return;
        case 0x38: wake_pol1_ = value; return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

/* INT output -> SA-1110 GPIO 1 (Linux jornada720.c: IRQ_GPIO1), rising-edge
   sensed. §11.1.1: a status clear pulses INT low and back high while sources
   remain pending — without that pulse_low_first re-edge on INTSTATCLR the
   guest ISR services one source and every later one hangs undelivered. */
void Sa1111Intc::DriveCascadeOutput(bool pulse_low_first) {
    auto& gpio = emu_.Get<Sa1110Gpio>();
    if (pulse_low_first) gpio.DriveInputPin(1, false);
    gpio.DriveInputPin(1, OutputAsserted());
}

void Sa1111Intc::RaiseInterrupt(uint8_t source) {
    if (source < 32) status0_ |= 1u << source;
    else             status1_ |= 1u << (source - 32);
    DriveCascadeOutput(false);
}

void Sa1111Intc::LowerInterrupt(uint8_t source) {
    if (source < 32) status0_ &= ~(1u << source);
    else             status1_ &= ~(1u << (source - 32));
    DriveCascadeOutput(false);
}

REGISTER_SERVICE(Sa1111Intc);
