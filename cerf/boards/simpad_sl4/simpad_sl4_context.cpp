#include "../board_context.h"

#include "simpad_sl4_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class SimpadSl4Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::SimpadSl4; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4Context, BoardContext);
