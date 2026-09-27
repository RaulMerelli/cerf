#include "../board_context.h"

#include "devemu_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class Smdk2410DevEmuContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::Devemu; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Smdk2410DevEmuContext, BoardContext);
