#include "wave_out_sink.h"

#include "../core/log.h"

WaveOutSink::~WaveOutSink() {
    Stop();
    CloseDevice();
    if (pace_timer_) CloseHandle(pace_timer_);
    if (ready_event_) CloseHandle(ready_event_);
}

void WaveOutSink::Start(ThreadCallback on_start, MessageHandler on_message,
                        const char* log_tag) {
    log_tag_     = log_tag;
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_      = std::thread([this, on_start, on_message] {
        ThreadMain(on_start, on_message);
    });
    WaitForSingleObject(ready_event_, INFINITE);
}

void WaveOutSink::Stop() {
    shutdown_.store(true, std::memory_order_release);
    if (thread_id_) PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void WaveOutSink::Post(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (thread_id_) PostThreadMessageW(thread_id_, message, wparam, lparam);
}

void WaveOutSink::ThreadMain(ThreadCallback on_start, MessageHandler on_message) {
    thread_id_ = GetCurrentThreadId();

    /* https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw
       CREATE_WAITABLE_TIMER_HIGH_RESOLUTION - without it the wait quantises to the
       system tick and a page shorter than that is reported late. */
    pace_timer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
    if (pace_timer_ == nullptr)
        pace_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (pace_timer_ == nullptr) {
        LOG(Caution, "%s: CreateWaitableTimer failed; block pacing has no clock\n", log_tag_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    /* Force the queue to exist before any PostThreadMessage races us. */
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready_event_);

    if (on_start) on_start();

    while (!shutdown_.load(std::memory_order_acquire)) {
        const DWORD w = MsgWaitForMultipleObjectsEx(1, &pace_timer_, INFINITE,
                                                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (w == WAIT_OBJECT_0) {
            MSG due{};
            due.message = kMsgPaceDue;
            if (on_message) on_message(due);
            continue;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            if (msg.message == kMsgArmSilent) {
                ArmSilentTimer(reinterpret_cast<WAVEHDR*>(msg.lParam));
                continue;
            }
            if (msg.message == kMsgCancelSilent) {
                CancelSilentTimers();
                continue;
            }
            MSG out = msg;
            if (msg.message == WM_TIMER) {
                if (WAVEHDR* hdr = TakeSilentTimer(msg.wParam)) {
                    out.message = MM_WOM_DONE;
                    out.wParam  = kSilentDone;
                    out.lParam  = reinterpret_cast<LPARAM>(hdr);
                }
            }
            if (on_message) on_message(out);
        }
    }
}

void WaveOutSink::ArmPaceDeadline(std::chrono::steady_clock::duration delay) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count();
    LARGE_INTEGER due;
    due.QuadPart = ns > 0 ? -(ns / 100) : -1;
    if (!SetWaitableTimer(pace_timer_, &due, 0, nullptr, nullptr, FALSE)) {
        LOG(Caution, "%s: SetWaitableTimer failed; a block would never be reported\n", log_tag_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void WaveOutSink::ArmSilentTimer(WAVEHDR* hdr) {
    if (!hdr) {
        LOG(Caution, "%s: silent completion for a null header\n", log_tag_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    std::lock_guard<std::mutex> lk(silent_mtx_);
    for (uint32_t i = 0; i < silent_count_; ++i) {
        if (silent_queue_[i] != hdr) continue;
        LOG(Caution, "%s: header %p is already awaiting a silent completion\n",
            log_tag_, static_cast<void*>(hdr));
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (silent_count_ == kSilentQueue) {
        LOG(Caution, "%s: %u silent blocks already pending\n", log_tag_, kSilentQueue);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    silent_queue_[silent_count_++] = hdr;
    if (silent_id_ == 0) ArmSilentHead();
}

std::chrono::steady_clock::duration
WaveOutSink::PeriodFor(uint32_t length) const {
    const uint64_t bytes_per_sec =
        static_cast<uint64_t>(req_rate_) * static_cast<uint32_t>(req_channels_) *
        (static_cast<uint32_t>(req_bits_) / 8u);
    if (bytes_per_sec == 0u) return std::chrono::steady_clock::duration::zero();
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(static_cast<double>(length) /
                                      static_cast<double>(bytes_per_sec)));
}

void WaveOutSink::ArmSilentHead() {
    const auto period = PeriodFor(silent_queue_[0]->dwBufferLength);
    if (period == std::chrono::steady_clock::duration::zero()) {
        LOG(Caution, "%s: cannot pace a silent block at %u Hz x %u ch x %u bit\n",
            log_tag_, req_rate_, req_channels_, req_bits_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(period).count();
    silent_id_ = SetTimer(nullptr, 0, ms == 0 ? 1u : static_cast<UINT>(ms), nullptr);
    if (silent_id_ == 0) {
        LOG(Caution, "%s: SetTimer failed; a silent block would never complete\n", log_tag_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void WaveOutSink::CancelSilentTimers() {
    std::lock_guard<std::mutex> lk(silent_mtx_);
    if (silent_id_ != 0) {
        KillTimer(nullptr, silent_id_);
        silent_id_ = 0;
    }
    for (uint32_t i = 0; i < silent_count_; ++i)
        Post(MM_WOM_DONE, kSilentDone, reinterpret_cast<LPARAM>(silent_queue_[i]));
    silent_count_ = 0;
}

WAVEHDR* WaveOutSink::TakeSilentTimer(UINT_PTR id) {
    std::lock_guard<std::mutex> lk(silent_mtx_);
    if (id != silent_id_ || silent_count_ == 0) return nullptr;
    KillTimer(nullptr, id);
    silent_id_ = 0;
    WAVEHDR* hdr = silent_queue_[0];
    --silent_count_;
    for (uint32_t i = 0; i < silent_count_; ++i)
        silent_queue_[i] = silent_queue_[i + 1];
    if (silent_count_ != 0) ArmSilentHead();
    return hdr;
}

void WaveOutSink::CloseDevice() {
    if (out_device_) {
        waveOutReset(out_device_);
        const MMRESULT r = waveOutClose(out_device_);
        if (r != MMSYSERR_NOERROR) {
            LOG(Caution, "%s: waveOutClose failed mmresult=%u - device handle leaked\n",
                log_tag_, r);
        }
        out_device_ = nullptr;
    }
}

bool WaveOutSink::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                              uint16_t bits, bool allow_resampler, bool busy) {
    req_rate_     = sample_rate_hz;
    req_channels_ = channels;
    req_bits_     = bits;
    if (FormatMatches(sample_rate_hz, channels, bits)) return true;
    if (out_device_) {
        if (busy) return true;          /* keep current device until buffers drain. */
        CloseDevice();
    }

    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = channels;
    fmt.nSamplesPerSec  = sample_rate_hz;
    fmt.wBitsPerSample  = bits;
    fmt.nBlockAlign     = static_cast<uint16_t>((bits / 8) * channels);
    fmt.nAvgBytesPerSec = sample_rate_hz * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    const DWORD flags = CALLBACK_THREAD |
                        (allow_resampler ? 0u : static_cast<DWORD>(WAVE_FORMAT_DIRECT));
    const MMRESULT r = waveOutOpen(&out_device_, WAVE_MAPPER, &fmt,
                                   thread_id_, 0, flags);
    if (r != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveOutOpen(%u Hz x %u ch x %u bit) failed mmresult=%u "
                "- silent-mode (blocks paced from their own duration)\n",
                log_tag_, sample_rate_hz, channels, bits, r);
        out_device_ = nullptr;
        return false;
    }
    open_rate_     = sample_rate_hz;
    open_channels_ = channels;
    open_bits_     = bits;
    LOG(Periph, "[%s] waveOut opened %u Hz x %u ch x %u bit\n",
        log_tag_, sample_rate_hz, channels, bits);
    return true;
}

bool WaveOutSink::Play(WAVEHDR* hdr) {
    if (!out_device_) {
        Post(kMsgArmSilent, 0, reinterpret_cast<LPARAM>(hdr));
        return true;
    }
    MMRESULT r = waveOutPrepareHeader(out_device_, hdr, sizeof(WAVEHDR));
    if (r != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveOutPrepareHeader failed mmresult=%u\n", log_tag_, r);
        return false;
    }
    r = waveOutWrite(out_device_, hdr, sizeof(WAVEHDR));
    if (r != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveOutWrite failed mmresult=%u\n", log_tag_, r);
        waveOutUnprepareHeader(out_device_, hdr, sizeof(WAVEHDR));
        return false;
    }
    return true;
}

void WaveOutSink::Unprepare(WAVEHDR* hdr) {
    if (out_device_ && hdr) waveOutUnprepareHeader(out_device_, hdr, sizeof(WAVEHDR));
}

void WaveOutSink::Reset() {
    if (out_device_) waveOutReset(out_device_);
    Post(kMsgCancelSilent);
}
