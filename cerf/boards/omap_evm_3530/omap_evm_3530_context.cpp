#include "../board_context.h"

#include "omap_3530_evm_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class OmapEvm3530Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::Omap3530Evm; }
};

}  /* namespace */

REGISTER_SERVICE_AS(OmapEvm3530Context, BoardContext);
