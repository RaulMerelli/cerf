#pragma once

#include "../../core/service.h"

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

class OdoArm720AudioPlayer : public Service {
public:
    using Service::Service;
    ~OdoArm720AudioPlayer() override;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetPlaybackEnabled(bool enabled);

private:
    void AudioThreadMain();
    void SubmitNextPage();

    static constexpr uint32_t kSampleRate     = 44100u;
    static constexpr uint16_t kChannels       = 2u;
    static constexpr uint16_t kBitsPerSample  = 16u;

    /* WAVEPDD.H:158 AUDIO_DMA_PAGE_SIZE; WAVEPDD.C:366 buffer = 4 pages. */
    static constexpr uint32_t kPageSize       = 2048u;
    static constexpr uint32_t kPagesPerBuffer = 4u;

    HWAVEOUT          out_device_     = nullptr;
    DWORD             audio_thread_id_ = 0;
    HANDLE            thread_ready_event_ = nullptr;
    std::thread       audio_thread_;
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> playback_enabled_{false};

    std::mutex state_mutex_;
    uint32_t   current_page_index_  = 0;
    uint32_t   submitted_pages_     = 0;

    struct PageSlot {
        WAVEHDR hdr;
        uint8_t bytes[2048];
    };
    PageSlot pages_[kPagesPerBuffer]{};
};
