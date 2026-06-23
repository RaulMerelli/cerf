#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

#include "../../core/service.h"
#include "../ps2_keyboard/ps2_keyboard.h"
#include "../ps2_mouse/ps2_mouse.h"

class StateWriter;
class StateReader;

class I8042Controller : public Service {
public:
    explicit I8042Controller(CerfEmulator& emu);

    bool ShouldRegister() override;

    uint8_t ReadData();              /* port 0x60 read */
    uint8_t ReadStatus();            /* port 0x64 read */
    void    WriteData(uint8_t v);    /* port 0x60 write */
    void    WriteCommand(uint8_t v); /* port 0x64 write */

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    enum class Pending { kNone, kWriteCmdByte, kAuxWrite };

    void DrainKbd();    /* pull keyboard responses into the output buffer (main) */
    void DrainMouse();  /* pull mouse responses into the output buffer (aux) */

    std::mutex mtx_;
    uint8_t    cmd_byte_ = 0;
    Pending    pending_  = Pending::kNone;

    /* Shared 8042 output buffer; each entry tags whether the byte is aux (mouse)
       data so the status AUX bit (0x20) reflects the byte at the head. */
    std::deque<std::pair<uint8_t, bool>> obuf_;

    Ps2Keyboard kbd_;
    Ps2Mouse    mouse_;
};
