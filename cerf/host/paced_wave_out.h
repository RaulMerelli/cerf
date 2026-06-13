#pragma once

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "wave_out_sink.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

/* Shared paced PCM-out (WaveOutSink + one block in flight): fires on_block_done per
   completion so a DMA audio ring advances at playback cadence; with no host device the
   completion is posted immediately so the ring still cycles. SetFormat is applied on the
   audio thread, where the waveOut open must run for CALLBACK_THREAD to deliver MM_WOM_DONE. */
class PacedWaveOut {
public:
    static constexpr uint32_t kMaxBlock = 0x2000u;   /* DMA LENGTH < 8 KB. */

    void Start(const char* log_tag, uint32_t rate_hz, uint16_t channels,
               uint16_t bits, bool allow_resampler);
    void Stop();
    void SetFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits);

    void BeginAudioOut(std::function<void()> on_block_done);
    void QueueOutput(const void* host_bytes, uint32_t length);
    void StopAudioOut();

private:
    void OnThreadMessage(const MSG& msg);

    static constexpr UINT kMsgSetFormat = WM_USER + 41;   /* re-EnsureFormat on the thread. */

    WaveOutSink           sink_;
    bool                  allow_resampler_ = false;
    WAVEHDR               header_ = {};
    uint8_t               buffer_[kMaxBlock] = {};
    std::mutex            audio_mutex_;
    std::function<void()> on_block_done_;
    std::atomic<bool>     output_active_{false};
    std::atomic<uint32_t> fmt_rate_{0};
    std::atomic<uint16_t> fmt_channels_{0};
    std::atomic<uint16_t> fmt_bits_{0};
};
