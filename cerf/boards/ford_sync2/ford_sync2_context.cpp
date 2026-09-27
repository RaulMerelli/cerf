#include "../board_context.h"

#include "ford_sync_2_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class FordSyncGen2Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::FordSync2; }
};

}  /* namespace */

REGISTER_SERVICE_AS(FordSyncGen2Context, BoardContext);
