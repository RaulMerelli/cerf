#include "../board_context.h"

#include "philips_velo_1_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class PhilipsVelo1Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::PhilipsVelo1; }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1Context, BoardContext);
