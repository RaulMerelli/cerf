#include "sharp_mobilon_hc4100_cmtt.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>

namespace {

constexpr uint32_t kCmdInit  = 0x01200010u;   /* nk.exe sub_91023A40 */
constexpr uint32_t kCmdRead  = 0x00002027u;   /* nk.exe sub_910235E0 */
constexpr uint32_t kCmdWrite = 0x00000017u;   /* nk.exe sub_910236E4 */

constexpr uint32_t kWriteReady = 0x00000200u;  /* CS3+0x00 bit polled by sub_910236E4 */

/* sub_910236E4 clocks each data byte out twice as byteswap(byte|0x97000000) then
   byteswap(byte|0x9F000000) (@0x9102376C lui 0x9700, @0x910237C4 or 0x08000000),
   so a data-clock write carries strobe byte 0x97 or 0x9F in bits 7-0. */
constexpr uint32_t kDataStrobeLo = 0x97u;
constexpr uint32_t kDataStrobeHi = 0x9Fu;

}  /* namespace */

bool SharpMobilonHc4100Cmtt::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
}

void SharpMobilonHc4100Cmtt::WriteDataPort(uint32_t value) {
    switch (value) {
        case kCmdInit:
        case kCmdRead:
            return;
        case kCmdWrite:
            write_ready_ = true;
            return;
        case 0u:
            write_ready_ = false;
            return;
        default:
            if ((value & 0xFFu) == kDataStrobeLo || (value & 0xFFu) == kDataStrobeHi) {
                write_ready_ = false;
                return;
            }
            LOG(Caution, "Cmtt CS3+0x00 unmodeled command 0x%08X\n", value);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

uint32_t SharpMobilonHc4100Cmtt::ReadDataPort() {
    return write_ready_ ? kWriteReady : 0u;
}

uint8_t SharpMobilonHc4100Cmtt::ReadStatusByte() {
    return 0u;
}

bool SharpMobilonHc4100Cmtt::ReadReady() const {
    return true;
}

REGISTER_SERVICE(SharpMobilonHc4100Cmtt);
