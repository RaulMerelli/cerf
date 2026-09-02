#pragma once

#include "../../core/service.h"
#include "../../host/paced_wave_out.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class StateReader;
class StateWriter;

namespace siemens_mp377 {

class SiemensMp377Sm501Regs;

// SM501 Databook ch. 11; siemens_mp377_v1040 VGXaudio.dll sub_2987AAC.
class SiemensMp377Sm501AudioOutput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    bool IsActive() const;
    uint64_t FramesEmitted() const;
    void QueueAc97PcmSample(uint32_t left20, uint32_t right20);
    void HandleDacPowerDown();
    void QueueBuffer(uint32_t offset);
    void SetPlaybackEnabled(bool enabled);
    void ResetFixedRate();
    void NoteIrqTypeRead();
    bool ConsumeIrqBit(uint8_t buffer_bit);

    std::unique_lock<std::mutex> LockForState();
    void SetPacerEnabled(bool enabled);
    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);

private:
    static constexpr uint32_t kBufferA = 0x0C3000u;
    static constexpr uint32_t kBufferB = 0x0C3600u;
    static constexpr uint32_t kBufferBytes = 0x00000600u;
    static constexpr uint32_t kDefaultRateHz = 48000u;
    static constexpr uint32_t kHostPacketMs = 20u;
    static constexpr uint32_t kHostStartupBlocks = 15u;
    static constexpr uint32_t kFramesPerFirmwareHalf = 192u;
    static constexpr uint32_t kHostPacketFrames = 576u;
    static constexpr uint32_t kHostPacketBytes = kHostPacketFrames * 4u;

    void QueueBufferData(uint32_t base, uint8_t buffer_bit);
    void CaptureBytes(const uint8_t* host, uint32_t bytes, uint32_t nonzero_samples, uint32_t peak_abs);
    void WriteCaptureWav();
    void QueuePendingBuffers();
    void PushIrqBit(uint8_t buffer_bit);
    uint8_t PopIrqBit();
    void ClearQueue();
    uint8_t QueuedBlocks();
    std::chrono::microseconds BlockPeriod() const;
    void StartPacer();
    void StopPacer();
    void PacerLoop();
    void ServiceClockTick();
    void ServiceCompletions();
    void RaiseIrq(uint8_t buffer_bit);

    PacedWaveOut output_;
    bool active_ = false;
    bool block_queued_ = false;
    uint8_t next_irq_bit_ = 1u;
    uint32_t rate_hz_ = kDefaultRateHz;
    uint32_t last_buffer_ = kBufferA;
    uint8_t ready_mask_ = 0u;
    uint8_t irq_fifo_[8]{};
    uint8_t irq_fifo_head_ = 0u;
    uint8_t irq_fifo_count_ = 0u;
    std::atomic<bool> id_pending_consume_{false};
    std::mutex queue_mutex_;
    std::mutex pacer_mutex_;
    std::condition_variable pacer_cv_;
    std::thread pacer_thread_;
    bool pacer_stop_ = false;
    bool pacer_enabled_ = false;
    std::array<uint8_t, kHostPacketBytes> host_packet_{};
    uint32_t host_packet_frames_ = 0u;
    uint32_t packet_nonzero_samples_ = 0u;
    uint32_t packet_peak_abs_ = 0u;
    uint64_t frames_emitted_total_ = 0u;
    uint64_t frames_clock_target_ = 0u;
    uint64_t clock_shortfall_count_ = 0u;
    std::mutex pcm_mutex_;
    std::vector<uint8_t> capture_pcm_;
    uint32_t capture_peak_abs_ = 0u;
    uint32_t capture_nonzero_samples_ = 0u;
    uint32_t capture_blocks_since_flush_ = 0u;
};

} // namespace siemens_mp377
