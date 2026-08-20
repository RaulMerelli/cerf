#pragma once

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "wave_out_sink.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

/* Shared paced PCM-out. Multiple WAVEHDRs are kept in flight so short guest DMA
   blocks are presented to waveOut as a continuous stream. QueueOutput always
   copies the block into a software FIFO; it never discards a new block merely
   because an older header is still queued. Small blocks can be coalesced into a
   host packet while retaining one producer completion per original block. */
class PacedWaveOut {
public:
    static constexpr uint32_t kMaxBlock = 0x2000u;   /* DMA LENGTH < 8 KB. */

    void Start(const char* log_tag, uint32_t rate_hz, uint16_t channels,
               uint16_t bits, bool allow_resampler);
    void Stop();
    void SetFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits);
    void SetPacketDurationMs(uint32_t duration_ms);
    void SetStartupBlockCount(uint32_t block_count);

    void BeginAudioOut(std::function<void()> on_block_done);
    bool QueueOutput(const void* host_bytes, uint32_t length);
    void StopAudioOut();

private:
    struct PendingBlock {
        std::vector<uint8_t> bytes;
        uint32_t completion_units = 1u;
        std::chrono::steady_clock::time_point queued_at{};
    };

    struct OutputSlot {
        WAVEHDR header = {};
        std::array<uint8_t, kMaxBlock> buffer = {};
        uint32_t completion_units = 0u;
        bool in_use = false;
    };

    void OnThreadMessage(const MSG& msg);
    void PumpOutputOnThread();
    uint32_t InFlightCountLocked() const;
    uint32_t TargetPacketBytes() const;

    static constexpr UINT kMsgSetFormat = WM_USER + 41;
    static constexpr UINT kMsgPumpOutput = WM_USER + 42;
    static constexpr size_t kOutputSlotCount = 16u;
    static constexpr uint32_t kCoalesceWaitMs = 25u;
    static constexpr uint32_t kStartupWaitMs = 80u;

    WaveOutSink sink_;
    bool allow_resampler_ = false;
    std::array<OutputSlot, kOutputSlotCount> slots_ = {};
    std::deque<PendingBlock> pending_blocks_;
    uint32_t startup_block_count_ = 0u;
    bool startup_ready_ = true;
    std::mutex audio_mutex_;
    std::function<void()> on_block_done_;
    std::atomic<bool> output_active_{false};
    std::atomic<uint32_t> fmt_rate_{0};
    std::atomic<uint16_t> fmt_channels_{0};
    std::atomic<uint16_t> fmt_bits_{0};
    std::atomic<uint32_t> packet_duration_ms_{0};
};

