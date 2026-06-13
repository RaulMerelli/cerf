#include "paced_wave_out.h"

#include <cstring>

void PacedWaveOut::Start(const char* log_tag, uint32_t rate_hz, uint16_t channels,
                         uint16_t bits, bool allow_resampler) {
    allow_resampler_ = allow_resampler;
    fmt_rate_.store(rate_hz, std::memory_order_release);
    fmt_channels_.store(channels, std::memory_order_release);
    fmt_bits_.store(bits, std::memory_order_release);
    sink_.Start(
        [this] {
            /* rate 0 = open lazily on the first SetFormat (avoids holding an idle host
               device on a board whose peripheral never plays). */
            if (fmt_rate_.load(std::memory_order_acquire) != 0)
                sink_.EnsureFormat(fmt_rate_.load(std::memory_order_acquire),
                                   fmt_channels_.load(std::memory_order_acquire),
                                   fmt_bits_.load(std::memory_order_acquire),
                                   allow_resampler_, /*busy=*/false);
        },
        [this](const MSG& msg) { OnThreadMessage(msg); },
        log_tag);
}

void PacedWaveOut::Stop() { sink_.Stop(); }

void PacedWaveOut::SetFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits) {
    fmt_rate_.store(rate_hz, std::memory_order_release);
    fmt_channels_.store(channels, std::memory_order_release);
    fmt_bits_.store(bits, std::memory_order_release);
    sink_.Post(kMsgSetFormat);
}

void PacedWaveOut::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgSetFormat) {
        sink_.EnsureFormat(fmt_rate_.load(std::memory_order_acquire),
                           fmt_channels_.load(std::memory_order_acquire),
                           fmt_bits_.load(std::memory_order_acquire),
                           allow_resampler_, /*busy=*/true);
        return;
    }
    if (msg.message != MM_WOM_DONE) return;
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        if (msg.lParam != 0 && sink_.IsOpen())
            sink_.Unprepare(reinterpret_cast<LPWAVEHDR>(msg.lParam));
        if (output_active_.load(std::memory_order_acquire)) cb = on_block_done_;
    }
    /* Invoke without the lock: the callback re-enters QueueOutput (caller's lock order). */
    if (cb) cb();
}

void PacedWaveOut::BeginAudioOut(std::function<void()> on_block_done) {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    on_block_done_ = std::move(on_block_done);
    output_active_.store(true, std::memory_order_release);
}

void PacedWaveOut::StopAudioOut() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    output_active_.store(false, std::memory_order_release);
    on_block_done_ = nullptr;
    sink_.Reset();
}

void PacedWaveOut::QueueOutput(const void* host_bytes, uint32_t length) {
    if (length == 0) return;
    if (length > kMaxBlock) length = kMaxBlock;

    std::lock_guard<std::mutex> lk(audio_mutex_);
    if (!output_active_.load(std::memory_order_acquire)) return;

    /* Silent mode (or device error): post completion immediately so the DMA delivers
       the next block and the ring keeps cycling. */
    if (!sink_.IsOpen()) { sink_.Post(MM_WOM_DONE, 0, 0); return; }

    /* One block in flight: if the prior block's MM_WOM_DONE hasn't freed header_ yet,
       post a synthetic completion so the DMA still advances the ring on it. */
    if (header_.dwFlags & WHDR_INQUEUE) { sink_.Post(MM_WOM_DONE, 0, 0); return; }

    std::memset(&header_, 0, sizeof(header_));
    std::memcpy(buffer_, host_bytes, length);
    header_.lpData         = reinterpret_cast<LPSTR>(buffer_);
    header_.dwBufferLength = length;
    if (!sink_.Play(&header_)) sink_.Post(MM_WOM_DONE, 0, 0);
}
