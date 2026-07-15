#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class IpaqGen1Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::IpaqGen1; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "iPAQ"; }
    /* CE3 imgdecmp DecompressImageIndirect rejects bpp not in {2,4,8,16,24}
       (imgdecmp.dll 0x1124300) and blanks the shell bitmaps at 32bpp. */
    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{240u, 320u};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(IpaqGen1Context, BoardContext);
