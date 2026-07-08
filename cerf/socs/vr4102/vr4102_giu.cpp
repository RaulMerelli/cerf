#include "vr4102_giu.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "vr4102_icu.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x0B000100u;

/* Register offsets from 0x0B000100 (UM ch.18 Table 18-2), low/high 16-bit pairs.
   PIOD (0x04/0x06 input-pin data), IOSELH (0x02) and PODATH (0x1E) are unreached
   by the guest and left FATAL; PIOD's read value is board-GPIO-wiring-dependent. */
constexpr uint32_t kOffIoSelL    = 0x00u;   /* GIUIOSELL    (18.2.1; IOS[15]/DCD# fixed input) */
constexpr uint32_t kOffPiodL     = 0x04u;   /* GIUPIODL     (18.2.3 p365; PIOD[15:0], D15 DCD# fixed input) */
constexpr uint32_t kOffPiodH     = 0x06u;   /* GIUPIODH     (18.2.4 p366; PIOD[31:16]) */
constexpr uint32_t kOffIntStatL  = 0x08u;   /* GIUINTSTATL  (18.2.5; W1C)  */
constexpr uint32_t kOffIntStatH  = 0x0Au;   /* GIUINTSTATH  (W1C) */
constexpr uint32_t kOffIntEnL    = 0x0Cu;   /* GIUINTENL    (18.2.7) */
constexpr uint32_t kOffIntEnH    = 0x0Eu;
constexpr uint32_t kOffIntTypL   = 0x10u;   /* GIUINTTYPL   (18.2.9;  1=edge/0=level) */
constexpr uint32_t kOffIntTypH   = 0x12u;
constexpr uint32_t kOffIntAlSelL = 0x14u;   /* GIUINTALSELL (18.2.11; 1=high/0=low active) */
constexpr uint32_t kOffIntAlSelH = 0x16u;
constexpr uint32_t kOffIntHtSelL = 0x18u;   /* GIUINTHTSELL (18.2.13; 1=hold/0=through) */
constexpr uint32_t kOffIntHtSelH = 0x1Au;
constexpr uint32_t kOffPoDatL    = 0x1Cu;   /* GIUPODATL    (18.2.15; reset 0xFFFF) */

}  /* namespace */

REGISTER_SERVICE(Vr4102Giu);

bool Vr4102Giu::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::VR4102;
}
void Vr4102Giu::OnReady() { emu_.Get<PeripheralDispatcher>().Register(this); }

uint16_t Vr4102Giu::ReadHalf(uint32_t addr) {
    std::lock_guard<std::mutex> lk(mtx_);
    return ReadHalfLocked(addr - kBase);
}
void Vr4102Giu::WriteHalf(uint32_t addr, uint16_t value) {
    std::lock_guard<std::mutex> lk(mtx_);
    WriteHalfLocked(addr - kBase, value);
}
/* The guest drives the int block with 32-bit stores over the L+H pair (nk.exe
   sub_9F002B94); each half hits its own register path. */
uint32_t Vr4102Giu::ReadWord(uint32_t addr) {
    std::lock_guard<std::mutex> lk(mtx_);
    const uint32_t off = addr - kBase;
    return static_cast<uint32_t>(ReadHalfLocked(off)) |
           (static_cast<uint32_t>(ReadHalfLocked(off + 2)) << 16);
}
void Vr4102Giu::WriteWord(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lk(mtx_);
    const uint32_t off = addr - kBase;
    WriteHalfLocked(off,     static_cast<uint16_t>(value & 0xFFFFu));
    WriteHalfLocked(off + 2, static_cast<uint16_t>(value >> 16));
}

uint16_t Vr4102Giu::ReadHalfLocked(uint32_t off) {
    switch (off) {
        case kOffIoSelL:    return iosel_l_;
        /* Output pins (IOS bit=1) read back the driven data; input pins (IOS bit=0)
           read the GPIO pin level (UM 18.2.3/18.2.4). D15/PIOD[15]=DCD# is fixed
           input, so iosel_l_ keeps bit15=0 and it always reads the pin level. */
        case kOffPiodL:     return static_cast<uint16_t>((piod_out_l_ & iosel_l_) |
                                   (static_cast<uint16_t>(level_) & static_cast<uint16_t>(~iosel_l_)));
        case kOffPiodH:     return static_cast<uint16_t>((piod_out_h_ & iosel_h_) |
                                   (static_cast<uint16_t>(level_ >> 16) & static_cast<uint16_t>(~iosel_h_)));
        case kOffIntStatL:  return intstat_l_;
        case kOffIntStatH:  return intstat_h_;
        case kOffIntEnL:    return inten_l_;
        case kOffIntEnH:    return inten_h_;
        case kOffIntTypL:   return inttyp_l_;
        case kOffIntTypH:   return inttyp_h_;
        case kOffIntAlSelL: return intalsel_l_;
        case kOffIntAlSelH: return intalsel_h_;
        case kOffIntHtSelL: return inthtsel_l_;
        case kOffIntHtSelH: return inthtsel_h_;
        case kOffPoDatL:    return podat_l_;
        default: HaltUnsupportedAccess("GIU ReadHalf", kBase + off, 0);
    }
}

void Vr4102Giu::WriteHalfLocked(uint32_t off, uint16_t value) {
    switch (off) {
        case kOffIoSelL: iosel_l_ = value & 0x7FFFu; return;  /* IOS[15]/DCD# fixed input */
        /* Only output pins (IOS bit=1) latch the write; input-pin writes are
           ignored (UM 18.2.3/18.2.4). */
        case kOffPiodL: piod_out_l_ = (piod_out_l_ & ~iosel_l_) | (value & iosel_l_); return;
        case kOffPiodH: piod_out_h_ = (piod_out_h_ & ~iosel_h_) | (value & iosel_h_); return;
        case kOffPoDatL: podat_l_ = value; return;

        /* GIUINTSTAT is write-1-to-clear; clearing a bit also drops any held
           change on that pin (UM 18.2.5/18.2.13). */
        case kOffIntStatL:
            intstat_l_ &= ~value; retained_l_ &= ~value;
            DriveIcuLocked(); return;
        case kOffIntStatH: intstat_h_ &= ~value; retained_h_ &= ~value; DriveIcuLocked(); return;

        /* Enabling a pin that holds a retained change asserts it now (UM 18.2.13). */
        case kOffIntEnL: {
            uint16_t promote = retained_l_ & value;
            intstat_l_ |= promote; retained_l_ &= ~promote; inten_l_ = value;
            DriveIcuLocked(); return;
        }
        case kOffIntEnH: {
            uint16_t promote = retained_h_ & value;
            intstat_h_ |= promote; retained_h_ &= ~promote; inten_h_ = value;
            DriveIcuLocked(); return;
        }

        case kOffIntTypL:   inttyp_l_   = value; return;
        case kOffIntTypH:   inttyp_h_   = value; return;
        case kOffIntAlSelL: intalsel_l_ = value; return;
        case kOffIntAlSelH: intalsel_h_ = value; return;
        case kOffIntHtSelL: inthtsel_l_ = value; return;
        case kOffIntHtSelH: inthtsel_h_ = value; return;

        default: HaltUnsupportedAccess("GIU WriteHalf", kBase + off, value);
    }
}

void Vr4102Giu::SetPinLevel(int pin, bool level) {
    if (pin < 0 || pin > 31) {
        LOG(Caution, "Vr4102Giu::SetPinLevel: pin %d has no interrupt register "
                     "(GIU int block covers GPIO[31:0])\n", pin);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    std::lock_guard<std::mutex> lk(mtx_);

    const bool     high = pin >= 16;
    const uint16_t bit  = static_cast<uint16_t>(1u << (pin & 15));
    uint16_t& stat = high ? intstat_h_ : intstat_l_;
    uint16_t& ret  = high ? retained_h_ : retained_l_;
    const uint16_t typ   = high ? inttyp_h_   : inttyp_l_;
    const uint16_t alsel = high ? intalsel_h_ : intalsel_l_;
    const uint16_t htsel = high ? inthtsel_h_ : inthtsel_l_;
    const uint16_t en    = high ? inten_h_    : inten_l_;

    const uint32_t pinbit = 1u << pin;
    const bool prev = (level_ & pinbit) != 0;
    if (level) level_ |= pinbit; else level_ &= ~pinbit;

    const bool edge = (typ & bit) != 0;
    bool triggered;
    if (edge) {
        triggered = (prev != level);                       /* both edges (UM 18.2.9) */
    } else {
        const bool active = (alsel & bit) != 0;            /* 1=high/0=low (UM 18.2.11) */
        triggered = (level == active);
    }
    const bool hold = (htsel & bit) != 0;

    if (triggered) {
        if (en & bit) stat |= bit;                         /* set iff enabled (UM 18.2.5) */
        else if (hold) ret |= bit;                         /* retain while disabled+hold */
    } else if (!edge && !hold) {
        stat &= ~bit;                                      /* level-through follows input */
    }
    DriveIcuLocked();
}

void Vr4102Giu::DriveIcuLocked() {
    auto& icu = emu_.Get<Vr4102Icu>();
    icu.SetGiuLow(intstat_l_);
    icu.SetGiuHigh(intstat_h_);
}

void Vr4102Giu::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write(iosel_l_); w.Write(podat_l_);
    w.Write(piod_out_l_); w.Write(piod_out_h_); w.Write(iosel_h_);
    w.Write(intstat_l_); w.Write(intstat_h_);
    w.Write(inten_l_);   w.Write(inten_h_);
    w.Write(inttyp_l_);  w.Write(inttyp_h_);
    w.Write(intalsel_l_);w.Write(intalsel_h_);
    w.Write(inthtsel_l_);w.Write(inthtsel_h_);
    w.Write(retained_l_);w.Write(retained_h_);
    w.Write(level_);
}
void Vr4102Giu::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read(iosel_l_); r.Read(podat_l_);
    r.Read(piod_out_l_); r.Read(piod_out_h_); r.Read(iosel_h_);
    r.Read(intstat_l_); r.Read(intstat_h_);
    r.Read(inten_l_);   r.Read(inten_h_);
    r.Read(inttyp_l_);  r.Read(inttyp_h_);
    r.Read(intalsel_l_);r.Read(intalsel_h_);
    r.Read(inthtsel_l_);r.Read(inthtsel_h_);
    r.Read(retained_l_);r.Read(retained_h_);
    r.Read(level_);
}
void Vr4102Giu::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    DriveIcuLocked();  /* re-assert the GIU indication into the restored ICU */
}
