#pragma once

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "wave_out_sink.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>

class PacedWaveOut {
public:
    static constexpr uint32_t kMaxBlock = 0x2000u;   /* DMA LENGTH < 8 KB. */

    void Start(const char* log_tag, uint32_t rate_hz, uint16_t channels,
               uint16_t bits, bool allow_resampler);
    void Stop();
    void SetFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits);

    void BeginAudioOut(std::function<void()> on_block_done);
    bool QueueOutput(const void* host_bytes, uint32_t length);
    void StopAudioOut();

private:
    void OnThreadMessage(const MSG& msg);
    void ApplyFormatLocked();
    bool HasFreeSlotLocked() const;
    void FlushPendingBlockLocked();
    void PlayLocked(const uint8_t* bytes, uint32_t length);
    bool ReleaseSlotLocked(uint32_t slot);
    void DrainPacedCallbacks();

    static constexpr UINT kMsgSetFormat  = WM_USER + 41;  /* re-EnsureFormat on the thread. */

    static constexpr uint32_t kSlots = 2;

    WaveOutSink           sink_;
    const char*           log_tag_ = "PacedWaveOut";
    bool                  allow_resampler_ = false;
    WAVEHDR               headers_[kSlots] = {};
    uint8_t               buffers_[kSlots][kMaxBlock] = {};
    bool                  slot_busy_[kSlots] = {};
    /* waveOutReset returns in-flight buffers to the application as MM_WOM_DONE:
       https://learn.microsoft.com/en-us/windows/win32/multimedia/mm-wom-done */
    uint32_t              slot_stale_[kSlots] = {};
    uint32_t              outstanding_ = 0;
    uint8_t               pending_buffer_[kSlots][kMaxBlock] = {};
    uint32_t              pending_length_[kSlots] = {};
    uint32_t              pending_count_ = 0;
    bool                  format_pending_ = false;
    static constexpr uint32_t kDueCap = 2 * kSlots;
    std::chrono::steady_clock::time_point due_[kDueCap] = {};
    uint32_t              due_n_ = 0;
    std::mutex            audio_mutex_;
    std::function<void()> on_block_done_;
    std::atomic<bool>     output_active_{false};
    std::atomic<uint32_t> fmt_rate_{0};
    std::atomic<uint16_t> fmt_channels_{0};
    std::atomic<uint16_t> fmt_bits_{0};
};
