#include "../../socs/s3c2410/s3c2410_pre_kernel_clocks.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "devemu_id.h"

namespace {

/* devemu_wm2003se nk.exe start 0x800410A4: MPLLCON = 0xA1031;
   0x80041078: CLKDIVN = 3. */
constexpr uint32_t kMpllCon = 0x000A1031u;
constexpr uint32_t kClkDivn = 0x00000003u;

class Smdk2410DevEmuPreKernelClocks : public S3C2410PreKernelClocks {
public:
    using S3C2410PreKernelClocks::S3C2410PreKernelClocks;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardId() == BoardId::Devemu;
    }

    uint32_t MpllCon() const override { return kMpllCon; }
    uint32_t ClkDivn() const override { return kClkDivn; }
};

}

REGISTER_SERVICE_AS(Smdk2410DevEmuPreKernelClocks, S3C2410PreKernelClocks);
