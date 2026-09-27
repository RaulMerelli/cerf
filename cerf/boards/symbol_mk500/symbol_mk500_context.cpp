#include "../board_context.h"

#include "symbol_mk500_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class SymbolMk500Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return BoardId::SymbolMk500; }
};

}

REGISTER_SERVICE_AS(SymbolMk500Context, BoardContext);
