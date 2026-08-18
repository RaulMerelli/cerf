#pragma once

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

/* Host waveOut output sink shared by the sound paths. EnsureFormat (waveOutOpen)
   MUST run on the audio thread - CALLBACK_THREAD delivers MM_WOM_DONE only there.
   Play/Unprepare/Reset are winmm-serialized and may run on any thread. */
class WaveOutSink {
public:
    using ThreadCallback  = std::function<void()>;
    using MessageHandler  = std::function<void(const MSG&)>;

    WaveOutSink() = default;
    ~WaveOutSink();
    WaveOutSink(const WaveOutSink&)            = delete;
    WaveOutSink& operator=(const WaveOutSink&) = delete;

    /* Spawn the audio thread, block until its queue exists. on_start runs once on
       the thread; on_message runs there per message except WM_QUIT. */
    void Start(ThreadCallback on_start, MessageHandler on_message,
               const char* log_tag);
    /* Signal WM_QUIT and join. Idempotent; call from OnShutdown so the thread
       stops before any peer it drives completion into is destroyed. */
    void Stop();

    DWORD ThreadId() const { return thread_id_; }
    /* PostThreadMessage to the audio thread; no-op before Start / after Stop. */
    void Post(UINT message, WPARAM wparam = 0, LPARAM lparam = 0) const;

    /* Open or re-open (on format change) the device; on the audio thread.
       allow_resampler=false => WAVE_FORMAT_DIRECT, rejecting non-native rates;
       busy=true holds a re-open until in-flight buffers drain. Returns IsOpen(). */
    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                      uint16_t bits, bool allow_resampler, bool busy);
    bool     IsOpen() const { return out_device_ != nullptr; }
    bool     FormatMatches(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) const {
        return out_device_ != nullptr && open_rate_ == sample_rate_hz &&
               open_channels_ == channels && open_bits_ == bits;
    }
    HWAVEOUT Device() const { return out_device_; }

    bool Play(WAVEHDR* hdr);
    void Unprepare(WAVEHDR* hdr);
    void Reset();

    static constexpr UINT kMsgPaceDue = WM_USER + 909;
    void ArmPaceDeadline(std::chrono::steady_clock::duration delay);

    std::chrono::steady_clock::duration PeriodFor(uint32_t length) const;

private:
    void ThreadMain(ThreadCallback on_start, MessageHandler on_message);
    void CloseDevice();
    void ArmSilentTimer(WAVEHDR* hdr);
    void ArmSilentHead();
    void CancelSilentTimers();
    WAVEHDR* TakeSilentTimer(UINT_PTR id);

    static constexpr UINT     kMsgArmSilent    = WM_USER + 907;
    static constexpr UINT     kMsgCancelSilent = WM_USER + 908;

public:
    static constexpr uint32_t kSilentQueue = 8;
    static constexpr WPARAM   kSilentDone  = 1;

private:

    std::mutex  silent_mtx_;
    WAVEHDR*    silent_queue_[kSilentQueue] = {};
    uint32_t    silent_count_ = 0;
    UINT_PTR    silent_id_    = 0;
    HANDLE      pace_timer_   = nullptr;
    uint32_t    req_rate_     = 0;
    uint16_t    req_channels_ = 0;
    uint16_t    req_bits_     = 0;

    DWORD             thread_id_   = 0;
    HANDLE            ready_event_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> shutdown_{false};

    HWAVEOUT    out_device_     = nullptr;
    uint32_t    open_rate_      = 0;
    uint16_t    open_channels_  = 0;
    uint16_t    open_bits_      = 0;
    const char* log_tag_        = "WaveOutSink";
};
