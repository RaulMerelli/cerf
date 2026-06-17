#include "../../socs/sa11xx/sa11xx_dma_audio_player.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* G138 wavedev (sub_1B81E34) points the playback DMA at the SA-1110 SSP (SSDR
   0x8007006C): TX DDAR 0x81C01BE8 matched by mask 0xFFFFFFF0 == 0x81C01BE0 (the
   RX DDAR 0x81C01BF9 falls outside the match). */
constexpr uint32_t kSspAudioTxDdarMask  = 0xFFFFFFF0u;
constexpr uint32_t kSspAudioTxDdarValue = 0x81C01BE0u;

/* 44100 Hz is the wavedev's hardware playback rate (dword_1B88090): sub_1B82784
   resamples every OS request to it (ratio = dword_1B88090 / requested_rate), so
   the playback DMA always carries 44100 Hz PCM. */
constexpr uint32_t kSampleRateHz = 44100u;

class SmartBookG138AudioPlayer : public Sa11xxDmaAudioPlayer {
public:
    using Sa11xxDmaAudioPlayer::Sa11xxDmaAudioPlayer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SmartBookG138;
    }

protected:
    Sa11xxAudioConfig AudioConfig() const override {
        return { kSspAudioTxDdarMask, kSspAudioTxDdarValue,
                 /*channels=*/2, /*bits=*/16, /*max_page=*/0x2000u,
                 /*allow_resampler=*/true, "SmartBookAudio" };
    }
    uint32_t SampleRateHz() override { return kSampleRateHz; }
};

}  /* namespace */

REGISTER_SERVICE(SmartBookG138AudioPlayer);
