#include "../board_context.h"

#include "smartbook_g138_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class SmartBookG138Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::SmartbookG138; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SmartBookG138Context, BoardContext);
