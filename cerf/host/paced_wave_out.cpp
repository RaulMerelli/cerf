#include "paced_wave_out.h"

#include <algorithm>
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

void PacedWaveOut::SetPacketDurationMs(uint32_t duration_ms) {
    packet_duration_ms_.store(duration_ms, std::memory_order_release);
    sink_.Post(kMsgPumpOutput);
}

void PacedWaveOut::SetStartupBlockCount(uint32_t block_count) {
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        startup_block_count_ = block_count;
        startup_ready_ = block_count == 0u ||
                         pending_blocks_.size() >= static_cast<size_t>(block_count);
    }
    sink_.Post(kMsgPumpOutput);
}

uint32_t PacedWaveOut::TargetPacketBytes() const {
    const uint64_t duration = packet_duration_ms_.load(std::memory_order_acquire);
    const uint64_t rate = fmt_rate_.load(std::memory_order_acquire);
    const uint64_t channels = fmt_channels_.load(std::memory_order_acquire);
    const uint64_t bits = fmt_bits_.load(std::memory_order_acquire);
    if (duration == 0u || rate == 0u || channels == 0u || bits == 0u) return 0u;
    const uint64_t bytes = (rate * channels * bits * duration) / 8000u;
    return static_cast<uint32_t>(std::min<uint64_t>(bytes, kMaxBlock));
}

uint32_t PacedWaveOut::InFlightCountLocked() const {
    uint32_t count = 0;
    for (const auto& slot : slots_)
        if (slot.in_use) ++count;
    return count;
}

void PacedWaveOut::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgSetFormat) {
        bool busy = false;
        {
            std::lock_guard<std::mutex> lk(audio_mutex_);
            busy = InFlightCountLocked() != 0u;
        }
        sink_.EnsureFormat(fmt_rate_.load(std::memory_order_acquire),
                           fmt_channels_.load(std::memory_order_acquire),
                           fmt_bits_.load(std::memory_order_acquire),
                           allow_resampler_, busy);
        PumpOutputOnThread();
        return;
    }
    if (msg.message == kMsgPumpOutput) {
        PumpOutputOnThread();
        return;
    }
    if (msg.message != MM_WOM_DONE) return;

    std::function<void()> cb;
    uint32_t completion_units = 0u;
    auto* done = reinterpret_cast<WAVEHDR*>(msg.lParam);
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        for (auto& slot : slots_) {
            if (&slot.header != done) continue;
            if (sink_.IsOpen()) sink_.Unprepare(&slot.header);
            completion_units = slot.completion_units;
            slot.completion_units = 0u;
            slot.in_use = false;
            std::memset(&slot.header, 0, sizeof(slot.header));
            if (output_active_.load(std::memory_order_acquire)) cb = on_block_done_;
            break;
        }
    }

    /* Refill before notifying the producer, preserving a continuous host queue. */
    PumpOutputOnThread();
    while (completion_units-- != 0u && cb &&
           output_active_.load(std::memory_order_acquire)) {
        cb();
    }
}

void PacedWaveOut::PumpOutputOnThread() {
    if (!output_active_.load(std::memory_order_acquire)) return;

    bool wait_for_startup = false;
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        if (!startup_ready_) {
            if (pending_blocks_.size() >= static_cast<size_t>(startup_block_count_)) {
                startup_ready_ = true;
            } else if (pending_blocks_.empty()) {
                return;
            } else {
                const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - pending_blocks_.front().queued_at).count();
                if (age_ms < static_cast<int64_t>(kStartupWaitMs))
                    wait_for_startup = true;
                else
                    startup_ready_ = true;
            }
        }
    }
    if (wait_for_startup) {
        Sleep(1u);
        sink_.Post(kMsgPumpOutput);
        return;
    }

    const uint32_t rate = fmt_rate_.load(std::memory_order_acquire);
    if (!sink_.IsOpen()) {
        /* A lazy-format source has not described its PCM yet. Preserve queued
           bytes until SetFormat instead of treating them as silent output. */
        if (rate == 0u) return;
        sink_.EnsureFormat(rate,
                           fmt_channels_.load(std::memory_order_acquire),
                           fmt_bits_.load(std::memory_order_acquire),
                           allow_resampler_, /*busy=*/false);
    }

    const uint32_t target_bytes = TargetPacketBytes();
    if (target_bytes != 0u) {
        bool wait_for_more = false;
        {
            std::lock_guard<std::mutex> lk(audio_mutex_);
            if (!pending_blocks_.empty()) {
                size_t pending_bytes = 0u;
                for (const auto& block : pending_blocks_) {
                    pending_bytes += block.bytes.size();
                    if (pending_bytes >= target_bytes) break;
                }
                const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - pending_blocks_.front().queued_at).count();
                wait_for_more = pending_bytes < target_bytes &&
                                age_ms < static_cast<int64_t>(kCoalesceWaitMs);
            }
        }
        if (wait_for_more) {
            Sleep(1u);
            sink_.Post(kMsgPumpOutput);
            return;
        }
    }

    uint32_t completed_without_device = 0;
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        if (!output_active_.load(std::memory_order_acquire)) return;
        cb = on_block_done_;

        if (!sink_.IsOpen()) {
            for (const auto& block : pending_blocks_)
                completed_without_device += block.completion_units;
            pending_blocks_.clear();
        } else {
            for (auto& slot : slots_) {
                if (pending_blocks_.empty()) break;
                if (slot.in_use) continue;

                uint32_t length = 0u;
                uint32_t completion_units = 0u;
                do {
                    const auto& front = pending_blocks_.front();
                    const uint32_t block_bytes = static_cast<uint32_t>(front.bytes.size());
                    if (length != 0u && length + block_bytes > slot.buffer.size()) break;
                    std::memcpy(slot.buffer.data() + length, front.bytes.data(), block_bytes);
                    length += block_bytes;
                    completion_units += front.completion_units;
                    pending_blocks_.pop_front();
                    if (target_bytes == 0u || length >= target_bytes) break;
                } while (!pending_blocks_.empty());

                if (length == 0u) break;
                std::memset(&slot.header, 0, sizeof(slot.header));
                slot.header.lpData = reinterpret_cast<LPSTR>(slot.buffer.data());
                slot.header.dwBufferLength = length;
                slot.completion_units = completion_units;
                slot.in_use = true;
                if (!sink_.Play(&slot.header)) {
                    slot.in_use = false;
                    slot.completion_units = 0u;
                    std::memset(&slot.header, 0, sizeof(slot.header));
                    completed_without_device += completion_units;
                }
            }
        }
    }

    /* Device failure/silent mode must still advance the emulated DMA ring. */
    while (completed_without_device-- != 0u && cb &&
           output_active_.load(std::memory_order_acquire)) {
        cb();
    }
}

void PacedWaveOut::BeginAudioOut(std::function<void()> on_block_done) {
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        on_block_done_ = std::move(on_block_done);
        startup_ready_ = startup_block_count_ == 0u;
        output_active_.store(true, std::memory_order_release);
    }
    sink_.Post(kMsgPumpOutput);
}

void PacedWaveOut::StopAudioOut() {
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        output_active_.store(false, std::memory_order_release);
        on_block_done_ = nullptr;
        pending_blocks_.clear();
        startup_ready_ = startup_block_count_ == 0u;
        for (auto& slot : slots_) slot.completion_units = 0u;
    }
    sink_.Reset();
}

bool PacedWaveOut::QueueOutput(const void* host_bytes, uint32_t length) {
    if (length == 0 || host_bytes == nullptr) return false;
    if (length > kMaxBlock) length = kMaxBlock;

    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        if (!output_active_.load(std::memory_order_acquire)) return false;
        const auto* first = static_cast<const uint8_t*>(host_bytes);
        PendingBlock block;
        block.bytes.assign(first, first + length);
        block.queued_at = std::chrono::steady_clock::now();
        pending_blocks_.push_back(std::move(block));
    }
    sink_.Post(kMsgPumpOutput);
    return true;
}
