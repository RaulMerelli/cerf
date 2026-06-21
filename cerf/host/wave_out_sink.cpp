#include "wave_out_sink.h"

#include "../core/log.h"

WaveOutSink::~WaveOutSink() {
    Stop();
    CloseDevice();
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

    /* Force the queue to exist before any PostThreadMessage races us. */
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready_event_);

    if (on_start) on_start();

    while (!shutdown_.load(std::memory_order_acquire) &&
           GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (on_message) on_message(msg);
    }
}

void WaveOutSink::CloseDevice() {
    if (out_device_) {
        waveOutClose(out_device_);
        out_device_ = nullptr;
    }
}

bool WaveOutSink::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                              uint16_t bits, bool allow_resampler, bool busy) {
    if (out_device_ && open_rate_ == sample_rate_hz &&
        open_channels_ == channels && open_bits_ == bits) {
        return true;
    }
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
                "- silent-mode (IRQ/pacing still run)\n",
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
    if (!out_device_) return false;
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
}
