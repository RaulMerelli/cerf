#include "../board_context.h"

#include "nec_mobilepro_900_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class NecMobilePro900Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::NecMobilepro900; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro900Context, BoardContext);
