#include "../../socs/sa11xx/sa11xx_dma_audio_player.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* iPAQ H3600 SSP audio transmit DMA: DA[31:8]=0x81C01B (SSP data port), the
   wavedev TX DDAR is 0x81C01B0x; mask to the port and exclude the receive
   channel. Fixed 22050 Hz stereo (SSP, no MCP rate divisor). */
constexpr uint32_t kDdarSspTxMask  = 0xFFFFFF00u;
constexpr uint32_t kDdarSspTxValue = 0x81C01B00u;

class IpaqGen1AudioPlayer : public Sa11xxDmaAudioPlayer {
public:
    using Sa11xxDmaAudioPlayer::Sa11xxDmaAudioPlayer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::IpaqGen1;
    }

protected:
    Sa11xxAudioConfig AudioConfig() const override {
        return { kDdarSspTxMask, kDdarSspTxValue,
                 /*channels=*/2, /*bits=*/16, /*max_page=*/16384u,
                 /*allow_resampler=*/false, "IpaqGen1Audio" };
    }
    uint32_t SampleRateHz() override { return 22050u; }
};

}  /* namespace */

REGISTER_SERVICE(IpaqGen1AudioPlayer);
