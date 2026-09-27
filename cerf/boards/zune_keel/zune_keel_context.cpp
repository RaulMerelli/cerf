#include "../board_context.h"

#include "zune_30_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class ZuneKeelContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::Zune30; }
};

}  /* namespace */

REGISTER_SERVICE_AS(ZuneKeelContext, BoardContext);
