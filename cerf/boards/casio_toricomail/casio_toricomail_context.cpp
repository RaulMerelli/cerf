#include "../board_context.h"

#include "casio_toricomail_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class CasioToricomailContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::CasioToricomail; }

    /* VR4121 UM Table 6-6 (p.172): PA above 0x1FFFFFFF mirrors into
       0x00000000-0x1FFFFFFF. */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioToricomailContext, BoardContext);
