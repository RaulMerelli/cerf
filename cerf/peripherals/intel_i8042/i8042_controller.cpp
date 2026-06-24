#include "i8042_controller.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../state/state_stream.h"

#include <cstddef>

namespace {
constexpr uint8_t kStsOutputBufFull = 0x01;
constexpr uint8_t kStsInputBufFull  = 0x02;   /* always 0 here: writes complete synchronously */
constexpr uint8_t kStsOutputBufAux  = 0x20;

constexpr uint8_t kCmdReadModeByte  = 0x20;
constexpr uint8_t kCmdWriteModeByte = 0x60;
constexpr uint8_t kCmdSelfTest      = 0xAA;   /* -> 0x55 */
constexpr uint8_t kCmdKbdIfaceTest  = 0xAB;   /* -> 0x00 */
constexpr uint8_t kCmdAuxDeviceWrite = 0xD4;  /* next data byte goes to the mouse */

constexpr uint8_t kModeKbdIntEnable = 0x01;
constexpr uint8_t kModeAuxIntEnable = 0x02;
}  // namespace

REGISTER_SERVICE(I8042Controller);

I8042Controller::I8042Controller(CerfEmulator& emu)
    : Service(emu),
      kbd_([this] { RaiseFromKbd(); }),
      mouse_([this] { RaiseFromMouse(); }) {}

bool I8042Controller::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::NecRockhopper;
}

void I8042Controller::DrainKbd() {
    while (kbd_.HasData()) obuf_.emplace_back(kbd_.ReadData(), false);
}

void I8042Controller::DrainMouse() {
    while (mouse_.HasData()) obuf_.emplace_back(mouse_.ReadData(), true);
}

void I8042Controller::PumpDevicesLocked() {
    DrainKbd();
    DrainMouse();
}

void I8042Controller::RaiseFromKbd() {
    if ((cmd_byte_ & kModeKbdIntEnable) && kbd_irq_sink_) kbd_irq_sink_();
}

void I8042Controller::RaiseFromMouse() {
    if ((cmd_byte_ & kModeAuxIntEnable) && aux_irq_sink_) aux_irq_sink_();
}

void I8042Controller::HostKeyboardScancodes(const uint8_t* bytes, size_t n) {
    kbd_.QueueScancodes(bytes, n);   /* on_data -> RaiseFromKbd raises IRQ1 */
}

void I8042Controller::HostMouseMotion(int dx, int dy, uint32_t button_mask) {
    mouse_.QueueMotion(dx, dy, button_mask);   /* on_data -> RaiseFromMouse raises IRQ12 */
}

uint8_t I8042Controller::ReadData() {
    bool raise_kbd = false, raise_aux = false;
    uint8_t b = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        PumpDevicesLocked();
        if (!obuf_.empty()) {
            b = obuf_.front().first;
            obuf_.pop_front();
        }
        /* Reloading the OBF with the next byte re-raises the IRQ so the ISR drains
           a multi-byte sequence one byte per interrupt (KeybdPdd_GetEventEx2 reads
           one byte per call). Single controller responses leave obuf empty -> no
           spurious raise. */
        if (!obuf_.empty()) {
            if (obuf_.front().second) raise_aux = true;
            else                      raise_kbd = true;
        }
    }
    if (raise_kbd) RaiseFromKbd();
    if (raise_aux) RaiseFromMouse();
    return b;
}

uint8_t I8042Controller::ReadStatus() {
    std::lock_guard<std::mutex> lk(mtx_);
    PumpDevicesLocked();
    uint8_t s = 0;
    if (!obuf_.empty()) {
        s |= kStsOutputBufFull;
        if (obuf_.front().second) s |= kStsOutputBufAux;
    }
    (void)kStsInputBufFull;   /* IBF stays clear: WriteData/WriteCommand consume input immediately */
    return s;
}

void I8042Controller::WriteData(uint8_t v) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (pending_) {
        case Pending::kWriteCmdByte:
            cmd_byte_ = v;
            pending_  = Pending::kNone;
            break;
        case Pending::kAuxWrite:
            mouse_.WriteCommand(v);
            pending_ = Pending::kNone;
            DrainMouse();
            break;
        case Pending::kNone:
            kbd_.WriteCommand(v);   /* a byte to port 0x60 with no command prefix = keyboard command */
            DrainKbd();
            break;
    }
}

void I8042Controller::WriteCommand(uint8_t v) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (v) {
        case kCmdReadModeByte:   obuf_.emplace_back(cmd_byte_, false);             break;
        case kCmdWriteModeByte:  pending_ = Pending::kWriteCmdByte;                break;
        case kCmdSelfTest:       obuf_.emplace_back(static_cast<uint8_t>(0x55), false); break;
        case kCmdKbdIfaceTest:   obuf_.emplace_back(static_cast<uint8_t>(0x00), false); break;
        case kCmdAuxDeviceWrite: pending_ = Pending::kAuxWrite;        break;
        default:
            LOG(Caution, "I8042Controller: unimplemented 8042 command 0x%02X\n", v);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void I8042Controller::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write(cmd_byte_);
    w.Write(static_cast<uint8_t>(pending_));
    w.Write<uint32_t>(static_cast<uint32_t>(obuf_.size()));
    for (const auto& e : obuf_) {
        w.Write(e.first);
        w.Write<uint8_t>(e.second ? 1u : 0u);
    }
    kbd_.SaveState(w);
    mouse_.SaveState(w);
}

void I8042Controller::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read(cmd_byte_);
    uint8_t p = 0; r.Read(p); pending_ = static_cast<Pending>(p);
    uint32_t n = 0; r.Read(n);
    obuf_.clear();
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t b = 0, aux = 0;
        r.Read(b); r.Read(aux);
        obuf_.emplace_back(b, aux != 0);
    }
    kbd_.RestoreState(r);
    mouse_.RestoreState(r);
}
