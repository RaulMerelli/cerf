#include "ite_it8368.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../state/state_stream.h"
#include "../pcmcia/pcmcia_slot.h"

namespace {

/* it8368reg.h IT8368_*_REG. */
constexpr uint32_t kRegGpioDataOut    = 0x00;
constexpr uint32_t kRegMfioDataOut    = 0x02;
constexpr uint32_t kRegGpioDir        = 0x04;
constexpr uint32_t kRegMfioDir        = 0x06;
constexpr uint32_t kRegMfioSel        = 0x0A;
constexpr uint32_t kRegGpioDataIn     = 0x0C;
constexpr uint32_t kRegMfioDataIn     = 0x0E;
constexpr uint32_t kRegGpioPosIntEn   = 0x10;
constexpr uint32_t kRegMfioPosIntEn   = 0x12;
constexpr uint32_t kRegGpioNegIntEn   = 0x14;
constexpr uint32_t kRegMfioNegIntEn   = 0x16;
constexpr uint32_t kRegGpioPosIntStat = 0x18;
constexpr uint32_t kRegMfioPosIntStat = 0x1A;
constexpr uint32_t kRegGpioNegIntStat = 0x1C;
constexpr uint32_t kRegMfioNegIntStat = 0x1E;
constexpr uint32_t kRegCtrl           = 0x20;

/* it8368reg.h IT8368_GPIODATAIN_MASK - twelve GPIOs, bits 12:0. */
constexpr uint16_t kGpioMask = 0x1FFF;

/* it8368reg.h IT8368_MFIO*_MASK - eleven MFIO pins, bits 10:0. */
constexpr uint16_t kMfioMask = 0x07FF;

/* it8368reg.h IT8368_MFIOSEL_VGAEN. */
constexpr uint16_t kMfioSelVgaEn = 0x0800;

/* it8368reg.h IT8368_PIN_*. */
constexpr uint16_t kPinCrdSw     = 0x1000;
constexpr uint16_t kPinCrdDet2   = 0x0800;
constexpr uint16_t kPinCrdDet1   = 0x0400;
constexpr uint16_t kPinCrdVccOn1 = 0x0080;
constexpr uint16_t kPinCrdVccOn0 = 0x0040;
constexpr uint16_t kPinCrdVppOn1 = 0x0020;
constexpr uint16_t kPinCrdVppOn0 = 0x0010;
constexpr uint16_t kPinBcrdRst   = 0x0001;

constexpr uint16_t kPinCrdVccMask = kPinCrdVccOn1 | kPinCrdVccOn0;
constexpr uint16_t kPinCrdVppMask = kPinCrdVppOn1 | kPinCrdVppOn0;

/* The pins the chip drives as outputs; it8368.c:262-265 enables exactly these
   and leaves the rest as card status inputs. */
constexpr uint16_t kDrivablePins = kPinCrdVccMask | kPinCrdVppMask | kPinBcrdRst;

/* CRDSW is an output-capable pin the Velo drives high (nk.exe sub_9F40F688);
   NetBSD never uses it and no ROM path reads it back, so it is accepted as a
   driven output but has no realized effect (ApplyOutputs models only the
   kDrivablePins set). */
constexpr uint16_t kOutputCapablePins = kDrivablePins | kPinCrdSw;

/* it8368reg.h IT8368_CTRL_*. */
constexpr uint16_t kCtrlFixAttrIo = 0x8000;
constexpr uint16_t kCtrlAddrSel   = 0x0010;
constexpr uint16_t kCtrlByteSwap  = 0x0008;
constexpr uint16_t kCtrlCardEn    = 0x0004;
constexpr uint16_t kCtrlGlobalEn  = 0x0002;
constexpr uint16_t kCtrlIntTriEn  = 0x0001;

constexpr uint16_t kCtrlKnown = kCtrlFixAttrIo | kCtrlAddrSel | kCtrlByteSwap |
                                kCtrlCardEn | kCtrlGlobalEn | kCtrlIntTriEn;

}  /* namespace */

bool IteIt8368::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const Board board = bd->GetBoard();
    return board == Board::PhilipsNino300 || board == Board::PhilipsVelo1;
}

void IteIt8368::OnReady() {
    emu_.Get<Pr31x00CardSpace>().ProvideCardBuffer(this);
}

bool IteIt8368::CardInterfaceEnabled() const { return (ctrl_ & kCtrlCardEn) != 0; }
bool IteIt8368::FixedAttributeIo()   const { return (ctrl_ & kCtrlFixAttrIo) != 0; }

/* An undriven card pin floats high, so an empty socket reads CRDDET1 and CRDDET2
   set (it8368.c:419 treats either as "no card"). A seated card drives CRDSENSE1/2,
   BCRDWP, BCRBVD2 and CRDSW from properties PcmciaSlot does not carry. */
uint16_t IteIt8368::GpioDataIn() const {
    if (slot_ && slot_->HasCard()) {
        LOG(Caution, "IteIt8368: card-present pin levels unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint16_t driven   = static_cast<uint16_t>(gpio_dataout_ & gpio_dir_);
    const uint16_t undriven = static_cast<uint16_t>(kGpioMask & ~gpio_dir_);
    return static_cast<uint16_t>(driven | undriven);
}

void IteIt8368::LatchInputEdges() {
    const uint16_t now  = GpioDataIn();
    const uint16_t diff = static_cast<uint16_t>(now ^ prev_datain_);
    gpio_posintstat_ |= static_cast<uint16_t>(diff & now);
    gpio_negintstat_ |= static_cast<uint16_t>(diff & ~now);
    prev_datain_ = now;
    UpdateInt();
}

/* GLOBALEN enables card and interrupt driving and INTTRIEN un-tristates the INT
   output (it8368.c:293-298); a latched edge on an enabled GPIO then asserts it.
   One pin carries every source - it8368.c:325-345 reads GPIONEGINTSTAT in the
   single handler to tell BCRDRDY from CRDDET2. */
bool IteIt8368::IntLevel() const {
    const uint16_t drive_bits = kCtrlGlobalEn | kCtrlIntTriEn;
    const bool driving = (ctrl_ & drive_bits) == drive_bits;
    const bool pending = ((gpio_posintstat_ & gpio_posinten_) |
                          (gpio_negintstat_ & gpio_neginten_)) != 0u;
    return driving && pending;
}

void IteIt8368::UpdateInt() {
    const bool level = IntLevel();
    if (level == int_asserted_) return;

    if (!int_sink_) {
        LOG(Caution, "IteIt8368: INT pin has no board wiring\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    int_asserted_ = level;
    int_sink_->OnIt8368IntLevel(level);
}

void IteIt8368::ApplyOutputs() {
    const uint16_t driven = static_cast<uint16_t>(gpio_dataout_ & gpio_dir_);

    if (driven & kPinCrdVppOn1) {
        LOG(Caution, "IteIt8368: CRDVPPON1 (Vpp 12 V) unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (driven & kPinCrdVppOn0) {
        LOG(Caution, "IteIt8368: CRDVPPON0 (Vpp = Vcc) unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if ((driven & kPinCrdVccMask) == kPinCrdVccMask) {
        LOG(Caution, "IteIt8368: CRDVCCON1 and CRDVCCON0 both driven\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    if (slot_) {
        slot_->SetPowered((driven & kPinCrdVccMask) != 0u);

        /* it8368.c:686-699 asserts BCRDRST, holds it, then clears it; the card
           leaves reset on that falling edge. */
        const bool rst_now = (driven & kPinBcrdRst) != 0u;
        if (rst_asserted_ && !rst_now) slot_->ResetCard();
        rst_asserted_ = rst_now;
    }

    LatchInputEdges();
}

uint16_t IteIt8368::ReadReg(uint32_t off) {
    switch (off) {
        case kRegGpioDataOut: return gpio_dataout_;
        case kRegGpioDir:     return gpio_dir_;

        /* nk.exe sub_9F40F39C reads MFIODATAOUT to save/restore bit 10 around the
           debug-probe strobe; the output register reads back its latch. */
        case kRegMfioDataOut: return mfio_dataout_;

        case kRegGpioDataIn:
            LatchInputEdges();
            return GpioDataIn();

        case kRegGpioPosIntStat:
            LatchInputEdges();
            return gpio_posintstat_;
        case kRegGpioNegIntStat:
            LatchInputEdges();
            return gpio_negintstat_;

        case kRegGpioPosIntEn: return gpio_posinten_;
        case kRegGpioNegIntEn: return gpio_neginten_;

        case kRegCtrl: return ctrl_;

        default:
            LOG(Caution, "IteIt8368::ReadReg unmodeled register $%02X\n", off);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void IteIt8368::WriteReg(uint32_t off, uint16_t value) {
    switch (off) {
        case kRegGpioDataOut:
            if (value & ~kOutputCapablePins) {
                LOG(Caution, "IteIt8368: GPIODATAOUT 0x%04X drives an input "
                        "pin\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            gpio_dataout_ = value;
            ApplyOutputs();
            return;

        case kRegGpioDir:
            if (value & ~kOutputCapablePins) {
                LOG(Caution, "IteIt8368: GPIODIR 0x%04X enables an output on a "
                        "card status pin\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            gpio_dir_ = value;
            ApplyOutputs();
            return;

        case kRegMfioSel:
            if (value & ~static_cast<uint16_t>(kMfioMask | kMfioSelVgaEn)) {
                LOG(Caution, "IteIt8368: MFIOSEL 0x%04X sets a reserved bit\n",
                    value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            if (value & kMfioSelVgaEn) {
                LOG(Caution, "IteIt8368: MFIOSEL 0x%04X enables VGA\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            mfio_sel_ = value;
            return;

        case kRegGpioPosIntEn:
            if (value & ~kGpioMask) {
                LOG(Caution, "IteIt8368: GPIOPOSINTEN 0x%04X enables a pin the "
                        "chip does not have\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            gpio_posinten_ = value;
            UpdateInt();
            return;

        case kRegGpioNegIntEn:
            if (value & ~kGpioMask) {
                LOG(Caution, "IteIt8368: GPIONEGINTEN 0x%04X enables a pin the "
                        "chip does not have\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            gpio_neginten_ = value;
            UpdateInt();
            return;

        case kRegMfioPosIntEn:
        case kRegMfioNegIntEn:
            if (value != 0u) {
                LOG(Caution, "IteIt8368: MFIO interrupt enable $%02X = 0x%04X on a "
                        "pin held at its reset function\n", off, value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            return;

        /* Write one to clear (it8368.c:337-339 clears a single latched pin). */
        case kRegGpioPosIntStat:
            gpio_posintstat_ &= static_cast<uint16_t>(~value);
            UpdateInt();
            return;
        case kRegGpioNegIntStat:
            gpio_negintstat_ &= static_cast<uint16_t>(~value);
            UpdateInt();
            return;

        /* Write one to clear (it8368.c:337-339). */
        case kRegMfioPosIntStat:
            mfio_posintstat_ &= static_cast<uint16_t>(~value);
            return;
        case kRegMfioNegIntStat:
            mfio_negintstat_ &= static_cast<uint16_t>(~value);
            return;

        case kRegCtrl:
            if (value & ~kCtrlKnown) {
                LOG(Caution, "IteIt8368: CTRL 0x%04X sets a reserved bit\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            /* ADDRSEL remaps LHA[14:13] onto the card window (it8368.c:238-241
               clears it), which would move every card offset the decoder derives. */
            if (value & kCtrlAddrSel) {
                LOG(Caution, "IteIt8368: CTRL ADDRSEL unmodeled\n");
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            /* This board crosses the chip's byte lanes, so a card cycle is only
               byte-correct while the chip swaps it back. */
            if ((value & kCtrlCardEn) && !(value & kCtrlByteSwap)) {
                LOG(Caution, "IteIt8368: CTRL 0x%04X enables the card interface "
                        "with BYTESWAP clear\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            ctrl_ = value;
            UpdateInt();
            return;

        case kRegGpioDataIn:
        case kRegMfioDataIn:
            LOG(Caution, "IteIt8368: write to input register $%02X\n", off);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);

        case kRegMfioDataOut:
            if (value & ~kMfioMask) {
                LOG(Caution, "IteIt8368: MFIODATAOUT 0x%04X sets a reserved "
                        "bit\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            mfio_dataout_ = value;
            return;

        case kRegMfioDir:
            if (value & ~kMfioMask) {
                LOG(Caution, "IteIt8368: MFIODIR 0x%04X sets a reserved bit\n",
                    value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            mfio_dir_ = value;
            return;

        default:
            LOG(Caution, "IteIt8368::WriteReg unmodeled register $%02X = 0x%04X\n",
                off, value);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void IteIt8368::SaveState(StateWriter& w) {
    w.Write(gpio_dataout_);
    w.Write(gpio_dir_);
    w.Write(gpio_posinten_);
    w.Write(gpio_neginten_);
    w.Write(gpio_posintstat_);
    w.Write(gpio_negintstat_);
    w.Write(mfio_posintstat_);
    w.Write(mfio_negintstat_);
    w.Write(mfio_dataout_);
    w.Write(mfio_dir_);
    w.Write(mfio_sel_);
    w.Write(ctrl_);
    w.Write(prev_datain_);
    w.Write<uint8_t>(rst_asserted_ ? 1u : 0u);
}

void IteIt8368::RestoreState(StateReader& r) {
    r.Read(gpio_dataout_);
    r.Read(gpio_dir_);
    r.Read(gpio_posinten_);
    r.Read(gpio_neginten_);
    r.Read(gpio_posintstat_);
    r.Read(gpio_negintstat_);
    r.Read(mfio_posintstat_);
    r.Read(mfio_negintstat_);
    r.Read(mfio_dataout_);
    r.Read(mfio_dir_);
    r.Read(mfio_sel_);
    r.Read(ctrl_);
    r.Read(prev_datain_);
    uint8_t rst = 0;
    r.Read(rst);
    rst_asserted_ = rst != 0u;

    /* Every peripheral downstream of the INT pin restores its own copy of the
       line - the pin level in Pr31x00Io's mfio_din_, the latched edge in the
       INTC's status - and restore order across peripherals is unspecified. */
    int_asserted_ = IntLevel();
}

REGISTER_SERVICE(IteIt8368);
