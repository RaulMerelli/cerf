#include "../board_context.h"

#include "falcon_4220_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class FalconPc3xxContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::Falcon4220; }
};

}  /* namespace */

REGISTER_SERVICE_AS(FalconPc3xxContext, BoardContext);
