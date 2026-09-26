#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/ti_tsc2017/ti_tsc2017_wiring.h"
#include "../../socs/imx6/imx6_i2c_bus.h"

#include <cstdint>

namespace {

/* IMX6DQRM Rev.2 Table 2-3: I2C3 at 0x021A8000. */
constexpr uint32_t kI2c3Base = 0x021A8000u;
/* TSC2017 SBAS472 Table 1: slave address 100100 A0; A0 is strapped high. */
constexpr uint8_t kTouchSlaveAddress = 0x49u;

class KtpMobileTsc2017Wiring final : public TiTsc2017Wiring {
public:
    using TiTsc2017Wiring::TiTsc2017Wiring;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && BoardContext::IsKtpMobile(board->GetBoard());
    }

    void Attach(Imx6I2cDevice* device) override {
        emu_.Get<Imx6I2cBus>().Register(device, kI2c3Base, kTouchSlaveAddress);
    }
};

}

REGISTER_SERVICE_AS(KtpMobileTsc2017Wiring, TiTsc2017Wiring);
