#include "pr31x00_io.h"

#include "pr31x00_intc.h"
#include "pr31x00_io_pins.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x10C00180u;

constexpr uint32_t kOffIoCtl     = 0x00u;   /* $180 */
constexpr uint32_t kOffMfioDout  = 0x04u;   /* $184 */
constexpr uint32_t kOffMfioDirec = 0x08u;   /* $188 */
constexpr uint32_t kOffMfioDin   = 0x0Cu;   /* $18C read-only */
constexpr uint32_t kOffMfioSel   = 0x10u;   /* $190 */
constexpr uint32_t kOffIoPd      = 0x14u;   /* $194 */
constexpr uint32_t kOffMfioPd    = 0x18u;   /* $198 */

/* I/O Control (§9.3.1): IODEBSEL[6:0]<30:24> IODIREC[6:0]<22:16> IODOUT[6:0]<14:8>
   IODIN[6:0]<6:0>(R); bits 31, 23, 15 and 7 reserved. */
constexpr uint32_t kIoCtlReserved  = 0x80808080u;
constexpr uint32_t kIoCtlWritable  = 0x7F7F7F00u;
constexpr uint32_t kIoDirecShift   = 16;
constexpr uint32_t kIoDoutShift    = 8;
constexpr uint32_t kIoPinMask      = 0x7Fu;   /* 7 general purpose I/O ports */

/* A direction bit set makes the port an output (§9.3.1, §9.3.3). A MFIOSEL bit set
   selects the pin as a bi-directional I/O port; cleared, the pin carries its
   standard/normal function instead (§9.3.5). */
constexpr uint32_t kMfioAllPorts   = 0xFFFFFFFFu;

/* I/O Power-Down (§9.3.6): IOPD[6:0]<6:0>; bits 31-7 reserved. */
constexpr uint32_t kIoPdReserved = 0xFFFFFF80u;

}  /* namespace */

bool Pr31x00Io::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const SocFamily soc = bd->GetSoc();
    return soc == SocFamily::PR31500 || soc == SocFamily::PR31700;
}

void Pr31x00Io::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Pr31x00Io::ReadWord(uint32_t addr) {
    switch (addr - kBase) {
        case kOffIoCtl: {
            /* IODIN reports the pin state whether the port drives it or not
               (§9.3.1): an output port reads back IODOUT, an input port reads
               the level the board drives onto the pin. */
            const uint32_t direc  = (io_ctl_ >> kIoDirecShift) & kIoPinMask;
            const uint32_t inputs = kIoPinMask & ~direc;
            uint32_t din = (io_ctl_ >> kIoDoutShift) & direc;
            if (inputs) {
                auto* pins = emu_.TryGet<Pr31x00IoPins>();
                if (!pins) {
                    HaltUnsupportedAccess("PR31x00 IO IODIN on an input port with no board wiring",
                                          addr, inputs);
                }
                din |= (io_din_ | pins->IoDin()) & inputs;
            }
            return io_ctl_ | din;
        }

        case kOffMfioDin: {
            /* MFIODIN returns each pin's logic state regardless of direction
               (§9.3.4): a pin selected bi-directional and set to output
               (direc & sel) reads back MFIODOUT, every other pin the level
               driven onto it (the board, or DriveMfioInput). */
            const uint32_t outputs = mfio_direc_ & mfio_sel_;
            const uint32_t inputs  = ~outputs;
            uint32_t din = mfio_dout_ & outputs;
            if (inputs) {
                auto* pins = emu_.TryGet<Pr31x00IoPins>();
                std::optional<uint32_t> board = pins ? pins->MfioDin() : std::nullopt;
                if (!board) {
                    HaltUnsupportedAccess("PR31x00 IO MFIODIN on an input pin with no board wiring",
                                          addr, inputs);
                }
                din |= (mfio_din_ | *board) & inputs;
            }
            return din;
        }

        case kOffMfioDout:  return mfio_dout_;
        case kOffMfioDirec: return mfio_direc_;
        case kOffMfioSel:   return mfio_sel_;
        case kOffIoPd:      return io_pd_;
        case kOffMfioPd:    return mfio_pd_;
        default:            HaltUnsupportedAccess("PR31x00 IO ReadWord", addr, 0);
    }
}

void Pr31x00Io::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - kBase) {
        case kOffIoCtl:
            if (value & kIoCtlReserved) {
                HaltUnsupportedAccess("PR31x00 IO IO_CTL reserved", addr, value);
            }
            io_ctl_ = value & kIoCtlWritable;
            return;

        case kOffMfioDout:  mfio_dout_  = value; return;
        case kOffMfioDirec: mfio_direc_ = value; return;
        case kOffMfioSel:   mfio_sel_   = value; return;
        case kOffMfioPd:    mfio_pd_    = value; return;

        case kOffIoPd:
            if (value & kIoPdReserved) {
                HaltUnsupportedAccess("PR31x00 IO IOPWRDN reserved", addr, value);
            }
            io_pd_ = value;
            return;

        case kOffMfioDin:   /* read-only (§9.3.4) */
        default:            HaltUnsupportedAccess("PR31x00 IO WriteWord", addr, value);
    }
}

/* MFIOPOSINT[n] latches a 0->1 transition of pin n and MFIONEGINT[n] a 1->0 one
   (§8.3.3, §8.3.4); they are Interrupt Status 3 and 4, so Clear/Status sets 2 and 3. */
void Pr31x00Io::DriveMfioInput(uint32_t pin, bool level) {
    const uint32_t bit = 1u << pin;
    if (((mfio_din_ & bit) != 0u) == level) return;

    mfio_din_ = level ? (mfio_din_ | bit) : (mfio_din_ & ~bit);
    emu_.Get<Pr31x00Intc>().SetPending(level ? 2u : 3u, bit);
}

/* A rising edge on pin n latches IOPOSINT[n] (Interrupt Status 5 bit 7+n), a
   falling edge IONEGINT[n] (bit n); both are in Interrupt Status 5, so Clear/
   Status set 4 (§8.3.5). */
void Pr31x00Io::DriveIoInput(uint32_t pin, bool level) {
    const uint32_t bit = 1u << pin;
    if (((io_din_ & bit) != 0u) == level) return;

    io_din_ = level ? (io_din_ | bit) : (io_din_ & ~bit);
    emu_.Get<Pr31x00Intc>().SetPending(4u, level ? (1u << (7u + pin)) : bit);
}

void Pr31x00Io::SaveState(StateWriter& w) {
    w.Write(io_ctl_);
    w.Write(mfio_dout_);
    w.Write(mfio_direc_);
    w.Write(mfio_sel_);
    w.Write(io_pd_);
    w.Write(mfio_pd_);
    w.Write(mfio_din_);
    w.Write(io_din_);
}

void Pr31x00Io::RestoreState(StateReader& r) {
    r.Read(io_ctl_);
    r.Read(mfio_dout_);
    r.Read(mfio_direc_);
    r.Read(mfio_sel_);
    r.Read(io_pd_);
    r.Read(mfio_pd_);
    r.Read(mfio_din_);
    r.Read(io_din_);
}

REGISTER_SERVICE(Pr31x00Io);
