#pragma once

#include "../../peripherals/peripheral_base.h"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

class S3C2410Iis : public Peripheral {
public:
    using Peripheral::Peripheral;
    ~S3C2410Iis() override;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x55000000u; }
    uint32_t MmioSize() const override { return 0x00000014u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Called by S3C2410Dma on DMA2 ON_OFF=1. Buffers BLOCK_SIZE bytes
       from host_bytes into the active WAVEHDR; when full, submits to
       waveOut. Always posts MM_WOM_DONE so the audio thread fires
       INT_DMA2 even when waveOut is unavailable (silent-mode boot). */
    void QueueOutput(const void* host_bytes, size_t length);

    /* Toggled by S3C2410Dma on DMA2 ON_OFF transitions. When false,
       MM_WOM_DONE handling suppresses INT_DMA2 — matches BSP
       m_outputDMA semantics. */
    void SetOutputDMA(bool on);

private:
    /* Audio loop, message-thread for waveOut callbacks. */
    void  AudioThreadMain();
    DWORD audio_thread_id_ = 0;

    /* waveOut state. Two headers for double-buffering, mirrors
       IOIIS::m_outputHeaders. */
    static constexpr uint32_t kBlockSize    = 0x800u;   /* 2048 bytes per DMA xfer */
    static constexpr uint32_t kQueueLength  = 10u;      /* blocks per WAVEHDR    */
    static constexpr uint32_t kBufferBytes  = kBlockSize * kQueueLength;
    static constexpr uint32_t kSampleRate   = 44100u;
    static constexpr uint16_t kChannels     = 2u;
    static constexpr uint16_t kBitsPerSamp  = 16u;

    HWAVEOUT  out_device_   = nullptr;
    uint8_t   out_buffer_[2 * kBufferBytes] = {};
    WAVEHDR   out_headers_[2] = {};
    WAVEHDR*  curr_out_header_ = nullptr;
    std::atomic<bool> switch_out_queue_{false};
    std::atomic<bool> output_dma_enabled_{false};

    /* Register file. Mirrors BSP IOIIS register layout — IISCON
       returns synthesised FIFO-ready bits per BSP convention. */
    mutable std::mutex state_mutex_;
    uint32_t iiscon_  = 0;
    uint32_t iismod_  = 0;
    uint32_t iispsr_  = 0;
    uint32_t iisfcon_ = 0;
    uint32_t iisfifo_ = 0;

    /* Worker thread + shutdown flag. */
    std::thread       audio_thread_;
    std::atomic<bool> shutdown_{false};
    HANDLE            thread_ready_event_ = nullptr;

    /* Reset / play helpers — caller holds state_mutex_. */
    bool QueueSwitchPossible() const;
    void SwitchQueue();
    void ResetCurrentQueue();
    void PlayCurrentQueue();
};
