#include "../board_context.h"

#include "nokia_lumia_800_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class NokiaLumia800Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::NokiaLumia800; }
};

}

REGISTER_SERVICE_AS(NokiaLumia800Context, BoardContext);
