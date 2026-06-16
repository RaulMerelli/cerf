#pragma once

#define NOMINMAX

#include "../../core/service.h"
#include "../../host/wave_out_sink.h"
#include "omap3530_sdma.h"

#include <cstdint>
#include <mutex>
#include <vector>

/* Claims the McBSP2-TX SDMA channel and plays its circular PCM pages on host
   waveOut, pacing the per-page frame interrupts to real playback. Sample rate
   comes from the TWL4030 CODEC_MODE.APLL_RATE the driver programmed over I2C. */
class Omap3530AudioPlayer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

private:
    /* TI ti_evm_3530 platform.reg: audio path is McBSP2, DmaTxSyncMap=0x21. */
    static constexpr uint32_t kMcbsp2TxSync = 33u;          /* SDMA_REQ_MCBSP2_TX. */
    static constexpr uint32_t kMcbsp2DxrPa  = 0x49022008u;  /* McBSP2 base 0x49022000 + DXR 0x08. */
    static constexpr int      kSlots        = 8;

    struct Slot {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
        uint32_t             seq       = 0;
        bool                 in_flight = false;
    };

    bool OnChannelClaim(const Omap3530SdmaBase::ChannelStart& s);
    void OnChannelStop(int channel);
    void OnThreadMessage(const MSG& msg);
    void StartStream();
    void StopStream();
    bool QueuePage();
    void OnPageDone(WAVEHDR* hdr);
    Slot* AllocSlot();
    uint32_t SampleRateHz() const;

    WaveOutSink sink_;
    std::mutex  mtx_;

    int      channel_    = -1;
    bool     active_     = false;
    uint32_t src_pa_     = 0;
    uint32_t page_bytes_ = 0;
    uint32_t page_count_ = 0;
    uint32_t seq_        = 0;     /* monotonic page sequence number. */

    /* slots_ is touched only on the audio thread (StartStream/QueuePage/
       OnPageDone run from OnThreadMessage); the JIT-thread claim/stop path
       writes only the mtx_-guarded fields above, so the ring needs no lock. */
    Slot     slots_[kSlots];
};
