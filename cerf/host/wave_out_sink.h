#pragma once

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

/* Host waveOut output sink shared by the sound paths. EnsureFormat (waveOutOpen)
   MUST run on the audio thread — CALLBACK_THREAD delivers MM_WOM_DONE only there.
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
    HWAVEOUT Device() const { return out_device_; }

    /* prepare + write `hdr`; on failure unprepare and return false. On-thread. */
    bool Play(WAVEHDR* hdr);
    void Unprepare(WAVEHDR* hdr);   /* unprepare a finished/failed header. */
    void Reset();                   /* waveOutReset — flush queued buffers. */

private:
    void ThreadMain(ThreadCallback on_start, MessageHandler on_message);
    void CloseDevice();

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
