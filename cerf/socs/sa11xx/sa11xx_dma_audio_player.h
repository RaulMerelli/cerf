#pragma once

#define NOMINMAX

#include "../../core/service.h"
#include "../../host/wave_out_sink.h"
#include "sa11xx_audio_config.h"
#include "sa11xx_dma.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

/* Shared SA-11x0 transmit-DMA -> host waveOut audio playback; subclasses supply
   the DMA DDAR match + PCM format (AudioConfig) and the playback rate. */
class Sa11xxDmaAudioPlayer : public Service {
public:
    using Service::Service;

    void OnReady() override;

protected:
    virtual Sa11xxAudioConfig AudioConfig() const = 0;
    virtual uint32_t          SampleRateHz()      = 0;
    /* Board hook reporting the codec/amp output-mute state. The SSP keeps
       clocking the TX DMA while output is muted (full-duplex record), so the
       player renders silence for any span where this returns true. */
    virtual bool              OutputMuted() const { return false; }

private:
    static constexpr uint32_t kPagesQueued = 4u;

    struct PendingPage {
        uint32_t dma_channel;
        bool     buffer_b;
        uint32_t src_pa;
        uint32_t byte_count;
        uint32_t rate_hz;
    };
    struct Slot {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
        uint32_t             dma_channel = 0;
        bool                 buffer_b    = false;
        bool                 in_flight   = false;
    };

    bool  OnDmaStart(const Sa11xxDma::ChannelState& st);
    void  OnThreadMessage(const MSG& msg);
    Slot* AllocSlotLocked();
    void  SubmitPage(const PendingPage& p);
    void  LoadIntoSlot(Slot& slot, const PendingPage& p);
    void  OnPageDone(LPWAVEHDR hdr);

    Sa11xxAudioConfig       cfg_{};
    WaveOutSink             sink_;
    std::mutex              slots_mtx_;
    Slot                    slots_[kPagesQueued];
    uint32_t                next_slot_ = 0;
    std::deque<PendingPage> page_queue_;
};
