#include "../../socs/sa11xx/sa11xx_dma_capture.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* iPAQ H3600 wavedev SSP receive-DMA DDAR = 0x81C01BF9 (AudioInitialize
   sub_F51924): same SSP data port DA[31:8]=0x81C01B as the transmit channel
   (0x81C01BE8), RW bit 0 = 1 = receive. Mask keeps bit 0 so the transmit player
   does not also claim it. Fixed 22050 Hz stereo (shared SSP clock). */
constexpr uint32_t kDdarSspRxMask  = 0xFFFFFF01u;
constexpr uint32_t kDdarSspRxValue = 0x81C01B01u;

class IpaqGen1AudioCapture : public Sa11xxDmaCapture {
public:
    using Sa11xxDmaCapture::Sa11xxDmaCapture;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::IpaqGen1;
    }

protected:
    Sa11xxAudioConfig AudioConfig() const override {
        return { kDdarSspRxMask, kDdarSspRxValue,
                 /*channels=*/2, /*bits=*/16, /*max_page=*/16384u,
                 /*allow_resampler=*/true, "IpaqGen1Capture" };
    }
    uint32_t SampleRateHz() override { return 22050u; }
};

}  /* namespace */

REGISTER_SERVICE(IpaqGen1AudioCapture);
