#include "paced_wave_out.h"

#include "../core/log.h"

#include <cstring>

void PacedWaveOut::Start(const char* log_tag, uint32_t rate_hz, uint16_t channels,
                         uint16_t bits, bool allow_resampler) {
    log_tag_         = log_tag;
    allow_resampler_ = allow_resampler;
    fmt_rate_.store(rate_hz, std::memory_order_release);
    fmt_channels_.store(channels, std::memory_order_release);
    fmt_bits_.store(bits, std::memory_order_release);
    sink_.Start(
        [this] {
            std::lock_guard<std::mutex> lk(audio_mutex_);
            ApplyFormatLocked();
        },
        [this](const MSG& msg) { OnThreadMessage(msg); },
        log_tag);
}

void PacedWaveOut::Stop() { sink_.Stop(); }

void PacedWaveOut::SetFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits) {
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        fmt_rate_.store(rate_hz, std::memory_order_release);
        fmt_channels_.store(channels, std::memory_order_release);
        fmt_bits_.store(bits, std::memory_order_release);
        format_pending_ = !sink_.FormatMatches(rate_hz, channels, bits);
    }
    sink_.Post(kMsgSetFormat);
}

void PacedWaveOut::ApplyFormatLocked() {
    const uint32_t rate     = fmt_rate_.load(std::memory_order_acquire);
    const uint16_t channels = fmt_channels_.load(std::memory_order_acquire);
    const uint16_t bits     = fmt_bits_.load(std::memory_order_acquire);
    if (rate == 0) { format_pending_ = false; return; }
    const bool busy = outstanding_ != 0;
    LOG(Periph, "[%s] SetFormat %u Hz x %u ch x %u bit busy=%d open=%d\n",
        log_tag_, rate, channels, bits, busy ? 1 : 0, sink_.IsOpen() ? 1 : 0);
    sink_.EnsureFormat(rate, channels, bits, allow_resampler_, busy);
    format_pending_ = sink_.IsOpen() && !sink_.FormatMatches(rate, channels, bits);
}

bool PacedWaveOut::HasFreeSlotLocked() const {
    for (uint32_t i = 0; i < kSlots; ++i)
        if (!slot_busy_[i] && slot_stale_[i] == 0u) return true;
    return false;
}

void PacedWaveOut::FlushPendingBlockLocked() {
    while (!format_pending_ && pending_count_ != 0 && HasFreeSlotLocked()) {
        const uint32_t length = pending_length_[0];
        LOG(Periph, "[%s] FlushPendingBlock len=%u open=%d\n",
            log_tag_, length, sink_.IsOpen() ? 1 : 0);
        PlayLocked(pending_buffer_[0], length);
        --pending_count_;
        for (uint32_t i = 0; i < pending_count_; ++i) {
            std::memcpy(pending_buffer_[i], pending_buffer_[i + 1], pending_length_[i + 1]);
            pending_length_[i] = pending_length_[i + 1];
        }
    }
}

void PacedWaveOut::PlayLocked(const uint8_t* bytes, uint32_t length) {
    uint32_t slot = kSlots;
    for (uint32_t i = 0; i < kSlots; ++i)
        if (!slot_busy_[i] && slot_stale_[i] == 0u) { slot = i; break; }
    if (slot == kSlots) {
        LOG(Caution, "[%s] no free slot with %u outstanding\n", log_tag_, outstanding_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    std::memset(&headers_[slot], 0, sizeof(headers_[slot]));
    std::memcpy(buffers_[slot], bytes, length);
    headers_[slot].lpData         = reinterpret_cast<LPSTR>(buffers_[slot]);
    headers_[slot].dwBufferLength = length;

    if (!sink_.Play(&headers_[slot])) {
        LOG(Caution, "[%s] sink refused a %u-byte block\n", log_tag_, length);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const auto now = std::chrono::steady_clock::now();
    slot_busy_[slot]    = true;
    ++outstanding_;

    if (due_n_ == kDueCap) {
        LOG(Caution, "[%s] %u page completions are already scheduled\n",
            log_tag_, due_n_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const bool was_empty = due_n_ == 0;
    const auto base = was_empty ? now : due_[due_n_ - 1];
    due_[due_n_++] = base + sink_.PeriodFor(length);
    if (was_empty) sink_.ArmPaceDeadline(due_[0] - now);
}

bool PacedWaveOut::ReleaseSlotLocked(uint32_t slot) {
    if (!slot_busy_[slot]) return false;
    slot_busy_[slot] = false;
    --outstanding_;
    return true;
}

void PacedWaveOut::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgSetFormat) {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        ApplyFormatLocked();
        FlushPendingBlockLocked();
        return;
    }
    if (msg.message == WaveOutSink::kMsgPaceDue) {
        DrainPacedCallbacks();
        return;
    }
    if (msg.message != MM_WOM_DONE) return;
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        if (msg.lParam != 0) {
            sink_.Unprepare(reinterpret_cast<LPWAVEHDR>(msg.lParam));
            for (uint32_t i = 0; i < kSlots; ++i) {
                if (msg.lParam != reinterpret_cast<LPARAM>(&headers_[i])) continue;
                if (slot_stale_[i] != 0u) {
                    --slot_stale_[i];
                    break;
                }
                ReleaseSlotLocked(i);
                break;
            }
        }
        if (format_pending_) ApplyFormatLocked();
        FlushPendingBlockLocked();
    }
}

void PacedWaveOut::DrainPacedCallbacks() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        if (due_n_ == 0) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < due_[0]) {
            sink_.ArmPaceDeadline(due_[0] - now);
            return;
        }

        --due_n_;
        for (uint32_t i = 0; i < due_n_; ++i) due_[i] = due_[i + 1];
        if (due_n_ != 0) sink_.ArmPaceDeadline(due_[0] - now);

        if (!output_active_.load(std::memory_order_acquire)) return;
        cb = on_block_done_;
    }
    if (cb) cb();
}


void PacedWaveOut::BeginAudioOut(std::function<void()> on_block_done) {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    on_block_done_ = std::move(on_block_done);
    pending_count_ = 0;
    due_n_         = 0;
    output_active_.store(true, std::memory_order_release);
}

void PacedWaveOut::StopAudioOut() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    output_active_.store(false, std::memory_order_release);
    on_block_done_ = nullptr;
    pending_count_ = 0;
    due_n_         = 0;
    format_pending_ = false;
    sink_.Reset();
    for (uint32_t i = 0; i < kSlots; ++i) {
        if (slot_busy_[i]) ++slot_stale_[i];
        slot_busy_[i] = false;
    }
    outstanding_ = 0;
}

bool PacedWaveOut::QueueOutput(const void* host_bytes, uint32_t length) {
    if (length == 0) return false;
    if (length > kMaxBlock) length = kMaxBlock;

    std::lock_guard<std::mutex> lk(audio_mutex_);
    if (!output_active_.load(std::memory_order_acquire)) return false;

    if (format_pending_ || !HasFreeSlotLocked()) {
        if (pending_count_ == kSlots) {
            LOG(Caution, "[%s] pending stash full (%u blocks) with %u outstanding; "
                         "a %u-byte block has nowhere to go\n",
                log_tag_, pending_count_, outstanding_, length);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        std::memcpy(pending_buffer_[pending_count_], host_bytes, length);
        pending_length_[pending_count_] = length;
        ++pending_count_;
        return true;
    }

    PlayLocked(static_cast<const uint8_t*>(host_bytes), length);
    return true;
}
