#include "../board_context.h"

#include "jornada_720_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class Jornada720Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::Jornada720; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada720Context, BoardContext);
