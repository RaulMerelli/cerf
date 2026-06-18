#pragma once

#define NOMINMAX

#include "../../core/service.h"
#include "../../host/wave_in_sink.h"
#include "sa11xx_audio_config.h"
#include "sa11xx_dma.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

/* Shared SA-11x0 receive-DMA <- host microphone capture; subclasses supply the
   DMA DDAR match + PCM format (AudioConfig) and the capture rate. */
class Sa11xxDmaCapture : public Service {
public:
    using Service::Service;

    void OnReady() override;
    void OnShutdown() override;

protected:
    virtual Sa11xxAudioConfig AudioConfig() const = 0;
    virtual uint32_t          SampleRateHz()      = 0;

private:
    enum Live : int { kUnknown = 0, kLive = 1, kDead = 2 };

    struct PendingPage {
        uint32_t channel;
        bool     buffer_b;
        uint32_t dst_pa;
        uint32_t byte_count;
    };

    bool OnDmaStart(const Sa11xxDma::ChannelState& st);   /* DMA thread.   */
    void OnThreadMessage(const MSG& msg);                 /* audio thread. */
    void EnsureOpenOnThread();                            /* audio thread. */
    void OnRecordedData(const uint8_t* data, uint32_t bytes); /* audio thread. */
    void DrainPending();                                  /* audio thread. */
    void WriteAndComplete(const PendingPage& p, const uint8_t* data);

    Sa11xxAudioConfig       cfg_{};
    WaveInSink              sink_;
    std::mutex              mtx_;
    std::deque<PendingPage> pending_;
    std::deque<uint8_t>     fifo_;
    std::atomic<int>        live_{kUnknown};
    std::atomic<bool>       open_posted_{false};
};
