#pragma once

#include "../../core/service.h"
#include "../../host/wave_out_sink.h"

#include <atomic>
#include <cstdint>
#include <mutex>

class OdoArm720AudioPlayer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void SetPlaybackEnabled(bool enabled);

private:
    void OnThreadMessage(const MSG& msg);
    void SubmitNextPage();

    static constexpr uint32_t kSampleRate     = 44100u;
    static constexpr uint16_t kChannels       = 2u;
    static constexpr uint16_t kBitsPerSample  = 16u;


    static constexpr uint32_t kPageSize       = 2048u;
    static constexpr uint32_t kPagesPerBuffer = 4u;

    WaveOutSink       sink_;
    std::atomic<bool> playback_enabled_{false};

    std::mutex state_mutex_;
    uint32_t   current_page_index_ = 0;
    uint32_t   submitted_pages_    = 0;

    struct PageSlot {
        WAVEHDR hdr;
        uint8_t bytes[2048];
    };
    PageSlot pages_[kPagesPerBuffer]{};
};
