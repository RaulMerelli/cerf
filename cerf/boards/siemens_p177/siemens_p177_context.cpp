#include "../board_context.h"

#include "siemens_p177_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class SiemensP177Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::SiemensP177; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensP177Context, BoardContext);
