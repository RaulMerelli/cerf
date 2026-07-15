#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class PhilipsNino300Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::PhilipsNino300; }
    SocFamily      GetSoc()            const override { return SocFamily::PR31700; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName() const override { return "Nino 300"; }
    /* 240x320 portrait: VIDEO_CTL2 HORZVAL=59, LINEVAL=319 (nk.exe sub_9F434078). */
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{240, 320};
    }

    /* CE 2.01 R3000 kernel masks mapped PFNs to 31 bits, so the default
       0xF0000000 window is unreachable; 0x20000000 is bit-31-clear and in the
       TX3912 undecoded span (system CS ends 0x1FFFFFFF, kuseg aliases 0x40000000+). */
    uint32_t GuestAdditionsWindowBase() const override { return 0x20000000u; }

    /* CE2.0 gwes creates only PAL_INDEXED palettes, so the device is 8bpp indexed. */
    uint32_t GetGuestAdditionsColorDepth() const override { return 8u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsNino300Context, BoardContext);
