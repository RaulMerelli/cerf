#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class CasioCassiopeiaE55Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()           const override { return Board::CasioCassiopeiaE55; }
    SocFamily      GetSoc()             const override { return SocFamily::VR4111; }
    CpuArch        GetCpuArch()         const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode()  const override { return RomPlacingMode::FlatContainer; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 240, 320 };
    }

    /* VR4111 UM (U13137EJ2V0UM00) Table 6-6: PA 0x04000000-0x09FFFFFF is
       "Space reserved for future use"; PA at/above 0x20000000 is a mirror of
       0x00000000-0x1FFFFFFF. */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioCassiopeiaE55Context, BoardContext);
