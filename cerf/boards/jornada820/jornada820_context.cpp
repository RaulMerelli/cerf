#include "../board_context.h"

#include "jornada_820_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class Jornada820Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::Jornada820; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820Context, BoardContext);
