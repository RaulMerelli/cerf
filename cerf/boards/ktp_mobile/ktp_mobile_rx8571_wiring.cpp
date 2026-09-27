#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/epson_rx8571/epson_rx8571_sa_wiring.h"
#include "../../socs/imx6/imx6_i2c_bus.h"

#include <cstdint>
#include "ktp_mobile_id.h"

namespace {

/* IMX6DQRM Rev.2 Table 2-3: I2C3 at 0x021A8000. */
constexpr uint32_t kI2c3Base = 0x021A8000u;
/* Epson RX-8571SA Application Manual ETM30E-02 pin description: DAS high selects 0110010. */
constexpr uint8_t kRtcSlaveAddress = 0x32u;

class KtpMobileRx8571Wiring final : public EpsonRx8571SaWiring {
public:
    using EpsonRx8571SaWiring::EpsonRx8571SaWiring;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && BoardId::IsKtpMobile(board->GetBoardId());
    }

    void Attach(Imx6I2cDevice* device) override {
        emu_.Get<Imx6I2cBus>().Register(device, kI2c3Base, kRtcSlaveAddress);
    }
};

}

REGISTER_SERVICE_AS(KtpMobileRx8571Wiring, EpsonRx8571SaWiring);
