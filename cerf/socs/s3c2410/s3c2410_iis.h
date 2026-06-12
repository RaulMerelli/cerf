#pragma once

#include "../../peripherals/peripheral_base.h"
#include "../../host/wave_out_sink.h"

#include <atomic>
#include <cstdint>
#include <mutex>

class S3C2410Iis : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    uint32_t MmioBase() const override { return 0x55000000u; }
    uint32_t MmioSize() const override { return 0x00000014u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Only the registers + the DMA-enable flag are machine state; the WAVEHDR
       ring, host buffers, and waveOut handles are transient host audio. */
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    /* S3C2410Dma DMA2 ON_OFF=1: buffers BLOCK_SIZE bytes into the active WAVEHDR
       and submits on full. Always posts MM_WOM_DONE so the audio thread fires
       INT_DMA2 even in a silent-mode boot. */
    void QueueOutput(const void* host_bytes, size_t length);

    /* S3C2410Dma DMA2 ON_OFF toggle; false suppresses INT_DMA2 (BSP m_outputDMA). */
    void SetOutputDMA(bool on);

private:
    void OnThreadMessage(const MSG& msg);

    static constexpr uint32_t kBlockSize    = 0x800u;   /* 2048 bytes per DMA xfer */
    static constexpr uint32_t kQueueLength  = 10u;      /* blocks per WAVEHDR */
    static constexpr uint32_t kBufferBytes  = kBlockSize * kQueueLength;
    static constexpr uint32_t kSampleRate   = 44100u;
    static constexpr uint16_t kChannels     = 2u;
    static constexpr uint16_t kBitsPerSamp  = 16u;

    /* Two WAVEHDRs for double-buffering, mirrors IOIIS::m_outputHeaders. */
    WaveOutSink       sink_;
    uint8_t           out_buffer_[2 * kBufferBytes] = {};
    WAVEHDR           out_headers_[2] = {};
    WAVEHDR*          curr_out_header_ = nullptr;
    std::atomic<bool> switch_out_queue_{false};
    std::atomic<bool> output_dma_enabled_{false};

    /* IISCON returns synthesised FIFO-ready bits per BSP read convention. */
    mutable std::mutex state_mutex_;
    uint32_t iiscon_  = 0;
    uint32_t iismod_  = 0;
    uint32_t iispsr_  = 0;
    uint32_t iisfcon_ = 0;
    uint32_t iisfifo_ = 0;

    /* caller holds state_mutex_. */
    bool QueueSwitchPossible() const;
    void SwitchQueue();
    void ResetCurrentQueue();
    void PlayCurrentQueue();
};
