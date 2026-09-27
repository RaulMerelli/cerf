#include "../board_context.h"

#include "casio_cassiopeia_em500_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class CasioCassiopeiaEm500Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::CasioCassiopeiaEm500; }

    /* VR4131 UM Fig 3-1: PA 0x20000000-0xFFFFFFFF mirrors 0x00000000-0x1FFFFFFF. */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioCassiopeiaEm500Context, BoardContext);
