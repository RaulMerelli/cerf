#include "../board_context.h"

#include "sharp_mobilon_hc4100_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class SharpMobilonHc4100Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::SharpMobilonHc4100; }

    uint32_t GuestAdditionsWindowBase() const override { return 0x20000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHc4100Context, BoardContext);
