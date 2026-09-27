#include "../board_context.h"

#include "ipaq_gen1_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class IpaqGen1Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::IpaqGen1; }
};

}  /* namespace */

REGISTER_SERVICE_AS(IpaqGen1Context, BoardContext);
