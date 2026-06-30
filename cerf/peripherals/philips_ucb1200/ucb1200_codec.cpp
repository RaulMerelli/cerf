#include "../../socs/sa11xx/sa11xx_mcp_codec.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* Philips UCB1200 codec on the SA-1100 MCP (Jornada 820). ADC reg 0x0A bit7
   START triggers a conversion -> reg 0x0B = bit15 valid | (10-bit<<5), polled
   by hplib.dll sub_11A252C (v>>5 & 0x3FF). The four inputs are battery/sensors
   (touch is on the nCS3 ASIC), so a nominal mid-scale sample is returned. */
class Ucb1200Codec : public Sa11xxMcpCodec {
public:
    using Sa11xxMcpCodec::Sa11xxMcpCodec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

    uint16_t ReadReg(uint8_t reg) override { return regs_[reg & 0xFu]; }

    void WriteReg(uint8_t reg, uint16_t value) override {
        reg &= 0xFu;
        regs_[reg] = value;
        if (reg == kAdcControl && (value & kAdcStart)) {
            regs_[kAdcData] = static_cast<uint16_t>(
                kAdcDataValid | ((kNominalSample & 0x3FFu) << 5));
        }
    }

    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    static constexpr uint8_t  kAdcControl    = 0x0Au;
    static constexpr uint8_t  kAdcData       = 0x0Bu;
    static constexpr uint16_t kAdcStart      = 0x0080u;  /* ADC_CR bit 7 */
    static constexpr uint16_t kAdcDataValid  = 0x8000u;  /* ADC_DATA bit 15 */
    static constexpr uint16_t kNominalSample = 0x0200u;  /* mid-scale 10-bit */

    uint16_t regs_[16] = {0};
};

}  /* namespace */

REGISTER_SERVICE_AS(Ucb1200Codec, Sa11xxMcpCodec);
