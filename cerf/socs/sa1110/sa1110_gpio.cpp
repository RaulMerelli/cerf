#include "sa1110_gpio.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "sa1110_intc.h"

bool Sa1110Gpio::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetSoc() == SocFamily::SA1110;
}

void Sa1110Gpio::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Sa1110Gpio::DriveInputPin(uint32_t pin, bool level) {
    const uint32_t bit = 1u << pin;
    std::unique_lock<std::mutex> lk(mtx_);
    const uint32_t old = input_state_;
    input_state_ = level ? (input_state_ | bit) : (input_state_ & ~bit);
    if (old == input_state_) return;

    /* §9.1.1.5: an edge matching GRER/GFER latches the GEDR bit. Edge
       detect applies to the pin state regardless of direction; CERF only
       drives input pins here. */
    const bool rising = level;
    const bool latched =
        (rising && (grer_ & bit)) || (!rising && (gfer_ & bit));
#if CERF_DEV_MODE
    LOG(Periph, "[Sa1110Gpio] DriveInputPin %u=%d grer=0x%08X gfer=0x%08X "
        "-> %s\n", pin, level ? 1 : 0, grer_, gfer_,
        latched ? "GEDR latch + IRQ" : "no edge config (dropped)");
#endif
    if (latched) {
        gedr_ |= bit;
        PublishEdgeSourcesLocked();
    }
}

/* ICPR sources 0..10 follow GEDR bits 0..10; source 11 is the OR of GEDR
   27..11 (§9.2.1.1). GEDR is the latch, so the ICPR view is a level. */
void Sa1110Gpio::PublishEdgeSourcesLocked() {
    const uint32_t gedr = gedr_;
    auto& intc = emu_.Get<Sa1110Intc>();
    intc.SetSourceLevel(0x7FFu, gedr & 0x7FFu);
    intc.SetSourceLevel(1u << 11,
                        (gedr & 0x0FFFF800u) ? (1u << 11) : 0u);
}

uint32_t Sa1110Gpio::ReadReg(uint32_t off) {
    std::unique_lock<std::mutex> lk(mtx_);
    switch (off) {
        case 0x00: return ReadGplrLocked();                    /* GPLR R-O */
        case 0x04: return gpdr_ & kPinMask;                    /* GPDR R/W */
        case 0x08: return 0;                                   /* GPSR W-O, read unpredictable */
        case 0x0C: return 0;                                   /* GPCR W-O, read unpredictable */
        case 0x10: return grer_ & kPinMask;                    /* GRER R/W */
        case 0x14: return gfer_ & kPinMask;                    /* GFER R/W */
        case 0x18: return gedr_ & kPinMask;                    /* GEDR R/W (W1C) */
        case 0x1C: return gafr_ & kPinMask;                    /* GAFR R/W */
        default:   return 0;
    }
}

void Sa1110Gpio::WriteReg(uint32_t off, uint32_t value) {
    const uint32_t v = value & kPinMask;
    std::unique_lock<std::mutex> lk(mtx_);
    switch (off) {
        case 0x00: break;                                      /* GPLR R-O, writes ignored */
        case 0x04: gpdr_ = v; break;
        case 0x08: output_state_ = (output_state_ | (v & gpdr_)) & kPinMask; break;
        case 0x0C: output_state_ &= ~(v & gpdr_); break;
        case 0x10: grer_ = v; break;
        case 0x14: gfer_ = v; break;
        case 0x18: gedr_ &= ~v; PublishEdgeSourcesLocked(); break;  /* W1C */
        case 0x1C: gafr_ = v; break;
        default:   break;
    }
}

uint8_t Sa1110Gpio::ReadByte(uint32_t addr) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (base > 0x1C) HaltUnsupportedAccess("ReadByte", addr, 0);
    return static_cast<uint8_t>((ReadReg(base) >> shift) & 0xFFu);
}

uint32_t Sa1110Gpio::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off > 0x1C || (off & 0x3u) != 0) HaltUnsupportedAccess("ReadWord", addr, 0);
    return ReadReg(off);
}

void Sa1110Gpio::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (base > 0x1C) HaltUnsupportedAccess("WriteByte", addr, value);
    const uint32_t cur     = ReadReg(base);
    const uint32_t cleared = cur & ~(0xFFu << shift);
    WriteReg(base, cleared | (static_cast<uint32_t>(value) << shift));
}

void Sa1110Gpio::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off > 0x1C || (off & 0x3u) != 0) HaltUnsupportedAccess("WriteWord", addr, value);
    WriteReg(off, value);
}

REGISTER_SERVICE(Sa1110Gpio);
