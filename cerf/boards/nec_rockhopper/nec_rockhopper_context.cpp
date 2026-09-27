#include "../board_context.h"

#include "nec_rockhopper_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class NecRockhopperContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::NecRockhopper; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecRockhopperContext, BoardContext);
