#include "../../socs/sa11xx/sa11xx_mcp_codec.h"

#include "ucb1x00_codec.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

class Ucb1x00McpAdapter : public Sa11xxMcpCodec {
public:
    using Sa11xxMcpCodec::Sa11xxMcpCodec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const Board b = bd->GetBoard();
        return b == Board::SimpadSl4 || b == Board::Jornada820;
    }

    uint16_t ReadReg(uint8_t reg) override {
        return emu_.Get<Ucb1x00Codec>().ReadReg(reg);
    }
    void WriteReg(uint8_t reg, uint16_t value) override {
        emu_.Get<Ucb1x00Codec>().WriteReg(reg, value);
    }
    void SaveState(StateWriter& w) override    { emu_.Get<Ucb1x00Codec>().SaveState(w); }
    void RestoreState(StateReader& r) override { emu_.Get<Ucb1x00Codec>().RestoreState(r); }
    void PostRestore() override                { emu_.Get<Ucb1x00Codec>().PostRestore(); }
};

}  /* namespace */

REGISTER_SERVICE_AS(Ucb1x00McpAdapter, Sa11xxMcpCodec);
