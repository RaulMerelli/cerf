#include "../board_context.h"

#include "odo_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class OdoArm720Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::Odo; }
};

}  /* namespace */

REGISTER_SERVICE_AS(OdoArm720Context, BoardContext);
