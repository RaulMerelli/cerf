#define NOMINMAX

#include "siemens_mp377_sm501_audio_output.h"
#include "siemens_mp377_sm501_audio_mcu.h"
#include "siemens_mp377_sm501_internal.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/audio_activity_widget.h"
#include "../../state/emulation_freeze.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace siemens_mp377 {

bool SiemensMp377Sm501AudioOutput::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Sm501AudioOutput::OnReady() {
    output_.Start("MP377SM501Audio", 0, 2, 16, true);
    StartPacer();
}

void SiemensMp377Sm501AudioOutput::OnShutdown() {
    StopPacer();
    WriteCaptureWav();
    output_.StopAudioOut();
    output_.Stop();
}

bool SiemensMp377Sm501AudioOutput::IsActive() const {
    return active_;
}

uint64_t SiemensMp377Sm501AudioOutput::FramesEmitted() const {
    return frames_emitted_total_;
}

void SiemensMp377Sm501AudioOutput::QueueAc97PcmSample(uint32_t left20, uint32_t right20) {
    if (!active_) return;
    (void)left20;
    (void)right20;
    ++frames_emitted_total_;
}

void SiemensMp377Sm501AudioOutput::HandleDacPowerDown() {
    if (!active_) return;
    SetPacerEnabled(false);
    active_ = false;
    id_pending_consume_.store(false);
    ClearQueue();
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        host_packet_frames_ = 0u;
        packet_nonzero_samples_ = 0u;
        packet_peak_abs_ = 0u;
        host_packet_.fill(0u);
    }
    output_.StopAudioOut();
}

void SiemensMp377Sm501AudioOutput::QueueBuffer(uint32_t off) {
    uint32_t base = 0u;
    uint8_t bit = 0u;
    if (off >= kBufferA && off < kBufferA + kBufferBytes) {
        base = kBufferA;
        bit = 1u;
    } else if (off >= kBufferB && off < kBufferB + kBufferBytes) {
        base = kBufferB;
        bit = 2u;
    } else {
        return;
    }
    if (off != base + kBufferBytes - 4u) return;
    if (!active_) {
        ready_mask_ |= bit;
        return;
    }
    QueueBufferData(base, bit);
}

void SiemensMp377Sm501AudioOutput::SetPlaybackEnabled(bool enabled) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    if (enabled && !active_) {
        active_ = true;
        id_pending_consume_.store(false);
        frames_clock_target_ = frames_emitted_total_;
        output_.SetFormat(rate_hz_, 2, 16);
        output_.BeginAudioOut({});
        QueuePendingBuffers();
        SetPacerEnabled(true);
        if (QueuedBlocks() == 0u &&
            (regs.regs_[kSm501IrqStatusReg / 4u] & SiemensMp377Sm501AudioMcu::kOutputIrqBit) == 0u) {
            RaiseIrq(next_irq_bit_);
        }
        emu_.Get<AudioActivityWidget>().NotePresent();
    } else if (!enabled && active_) {
        SetPacerEnabled(false);
        active_ = false;
        frames_clock_target_ = frames_emitted_total_;
        block_queued_ = false;
        ready_mask_ = 0u;
        id_pending_consume_.store(false);
        ClearQueue();
        output_.StopAudioOut();
    }
}

void SiemensMp377Sm501AudioOutput::NoteIrqTypeRead() {
    id_pending_consume_.store(false);
}

void SiemensMp377Sm501AudioOutput::ResetFixedRate() {
    rate_hz_ = kDefaultRateHz;
}

bool SiemensMp377Sm501AudioOutput::ConsumeIrqBit(uint8_t buffer_bit) {
    buffer_bit &= 0x03u;
    if (buffer_bit == 0u) return false;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    const uint8_t capacity = static_cast<uint8_t>(sizeof(irq_fifo_));
    for (uint8_t i = 0u; i < irq_fifo_count_; ++i) {
        const uint8_t idx = static_cast<uint8_t>((irq_fifo_head_ + i) % capacity);
        if (irq_fifo_[idx] != buffer_bit) continue;
        for (uint8_t j = i; j + 1u < irq_fifo_count_; ++j) {
            const uint8_t from = static_cast<uint8_t>((irq_fifo_head_ + j + 1u) % capacity);
            const uint8_t to = static_cast<uint8_t>((irq_fifo_head_ + j) % capacity);
            irq_fifo_[to] = irq_fifo_[from];
        }
        const uint8_t tail = static_cast<uint8_t>((irq_fifo_head_ + irq_fifo_count_ - 1u) % capacity);
        irq_fifo_[tail] = 0u;
        --irq_fifo_count_;
        block_queued_ = irq_fifo_count_ != 0u;
        return true;
    }
    return false;
}

std::unique_lock<std::mutex> SiemensMp377Sm501AudioOutput::LockForState() {
    return std::unique_lock<std::mutex>(pacer_mutex_);
}

void SiemensMp377Sm501AudioOutput::SaveState(StateWriter& w) {
    w.Write(active_);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        w.Write(block_queued_);
        w.Write(next_irq_bit_);
        w.Write(rate_hz_);
        w.Write(last_buffer_);
        w.Write(ready_mask_);
        w.WriteBytes(irq_fifo_, sizeof(irq_fifo_));
        w.Write(irq_fifo_head_);
        w.Write(irq_fifo_count_);
    }
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    w.WriteBytes(host_packet_.data(), host_packet_.size());
    w.Write(host_packet_frames_);
    w.Write(packet_nonzero_samples_);
    w.Write(packet_peak_abs_);
    w.Write(frames_emitted_total_);
    w.Write(frames_clock_target_);
    w.Write(clock_shortfall_count_);
}

void SiemensMp377Sm501AudioOutput::RestoreState(StateReader& r) {
    r.Read(active_);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        r.Read(block_queued_);
        r.Read(next_irq_bit_);
        r.Read(rate_hz_);
        r.Read(last_buffer_);
        r.Read(ready_mask_);
        r.ReadBytes(irq_fifo_, sizeof(irq_fifo_));
        r.Read(irq_fifo_head_);
        r.Read(irq_fifo_count_);
        irq_fifo_head_ = static_cast<uint8_t>(irq_fifo_head_ % sizeof(irq_fifo_));
        if (irq_fifo_count_ > sizeof(irq_fifo_)) irq_fifo_count_ = static_cast<uint8_t>(sizeof(irq_fifo_));
        block_queued_ = irq_fifo_count_ != 0u;
    }
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        r.ReadBytes(host_packet_.data(), host_packet_.size());
        r.Read(host_packet_frames_);
        r.Read(packet_nonzero_samples_);
        r.Read(packet_peak_abs_);
        r.Read(frames_emitted_total_);
        r.Read(frames_clock_target_);
        r.Read(clock_shortfall_count_);
        if (host_packet_frames_ > kHostPacketFrames) host_packet_frames_ = 0u;
    }
    id_pending_consume_.store(false);
    if (active_) {
        output_.SetFormat(rate_hz_, 2, 16);
        output_.BeginAudioOut({});
        SetPacerEnabled(true);
    }
}

void SiemensMp377Sm501AudioOutput::QueueBufferData(uint32_t base, uint8_t buffer_bit) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    std::array<uint8_t, kBufferBytes / 2u> host{};
    uint32_t out = 0u;
    uint32_t nonzero_samples = 0u;
    uint32_t peak_abs = 0u;
    for (uint32_t i = 0u; i + 7u < kBufferBytes; i += 8u) {
        const uint32_t left_word = regs.regs_[(base + i) / 4u];
        const uint32_t right_word = regs.regs_[(base + i + 4u) / 4u];
        const int16_t left = static_cast<int16_t>((left_word >> 4u) & 0xFFFFu);
        const int16_t right = static_cast<int16_t>((right_word >> 4u) & 0xFFFFu);
        const uint32_t left_abs =
            left < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(left)) : static_cast<uint32_t>(left);
        const uint32_t right_abs =
            right < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(right)) : static_cast<uint32_t>(right);
        if (left != 0) ++nonzero_samples;
        if (right != 0) ++nonzero_samples;
        peak_abs = std::max(peak_abs, std::max(left_abs, right_abs));
        host[out++] = static_cast<uint8_t>(left);
        host[out++] = static_cast<uint8_t>(static_cast<uint16_t>(left) >> 8u);
        host[out++] = static_cast<uint8_t>(right);
        host[out++] = static_cast<uint8_t>(static_cast<uint16_t>(right) >> 8u);
    }
    last_buffer_ = base;
    output_.QueueOutput(host.data(), out);
    CaptureBytes(host.data(), out, nonzero_samples, peak_abs);
    emu_.Get<AudioActivityWidget>().NotePresent();
    PushIrqBit(buffer_bit);
}

void SiemensMp377Sm501AudioOutput::CaptureBytes(const uint8_t* host, uint32_t bytes, uint32_t nonzero_samples,
                                                uint32_t peak_abs) {
    const bool first_nonzero = capture_nonzero_samples_ == 0u && nonzero_samples != 0u;
    capture_pcm_.insert(capture_pcm_.end(), host, host + bytes);
    capture_nonzero_samples_ += nonzero_samples;
    capture_peak_abs_ = std::max(capture_peak_abs_, peak_abs);
    ++capture_blocks_since_flush_;
    if (first_nonzero || capture_blocks_since_flush_ >= 32u) {
        WriteCaptureWav();
        capture_blocks_since_flush_ = 0u;
    }
}

void SiemensMp377Sm501AudioOutput::WriteCaptureWav() {
    if (capture_pcm_.empty()) return;
    const char* path = "tmp/mp377_audio_capture_last.wav";
    std::error_code ec;
    std::filesystem::create_directories("tmp", ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        LOG(Caution, "MP377 SM501 audio capture: failed_open path=%s bytes=%u\n", path,
            static_cast<uint32_t>(capture_pcm_.size()));
        return;
    }
    const uint16_t channels = 2u;
    const uint16_t bits_per_sample = 16u;
    const uint16_t block_align = channels * (bits_per_sample / 8u);
    const uint32_t byte_rate = rate_hz_ * block_align;
    const uint32_t data_bytes = static_cast<uint32_t>(capture_pcm_.size());
    const uint32_t riff_bytes = 36u + data_bytes;
    auto write_u16 = [&out](uint16_t v) {
        out.put(static_cast<char>(v & 0xFFu));
        out.put(static_cast<char>((v >> 8u) & 0xFFu));
    };
    auto write_u32 = [&out](uint32_t v) {
        out.put(static_cast<char>(v & 0xFFu));
        out.put(static_cast<char>((v >> 8u) & 0xFFu));
        out.put(static_cast<char>((v >> 16u) & 0xFFu));
        out.put(static_cast<char>((v >> 24u) & 0xFFu));
    };
    out.write("RIFF", 4);
    write_u32(riff_bytes);
    out.write("WAVEfmt ", 8);
    write_u32(16u);
    write_u16(1u);
    write_u16(channels);
    write_u32(rate_hz_);
    write_u32(byte_rate);
    write_u16(block_align);
    write_u16(bits_per_sample);
    out.write("data", 4);
    write_u32(data_bytes);
    out.write(reinterpret_cast<const char*>(capture_pcm_.data()), capture_pcm_.size());
}

void SiemensMp377Sm501AudioOutput::QueuePendingBuffers() {
    const uint8_t ready = ready_mask_;
    ready_mask_ = 0u;
    if ((ready & 0x01u) != 0u) QueueBufferData(kBufferA, 1u);
    if ((ready & 0x02u) != 0u) QueueBufferData(kBufferB, 2u);
}

void SiemensMp377Sm501AudioOutput::PushIrqBit(uint8_t buffer_bit) {
    buffer_bit &= 0x03u;
    if (buffer_bit == 0u) return;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    const uint8_t capacity = static_cast<uint8_t>(sizeof(irq_fifo_));
    if (irq_fifo_count_ >= capacity) {
        irq_fifo_[irq_fifo_head_] = buffer_bit;
        irq_fifo_head_ = static_cast<uint8_t>((irq_fifo_head_ + 1u) % capacity);
        irq_fifo_count_ = capacity;
    } else {
        const uint8_t tail = static_cast<uint8_t>((irq_fifo_head_ + irq_fifo_count_) % capacity);
        irq_fifo_[tail] = buffer_bit;
        ++irq_fifo_count_;
    }
    block_queued_ = irq_fifo_count_ != 0u;
}

uint8_t SiemensMp377Sm501AudioOutput::PopIrqBit() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (irq_fifo_count_ == 0u) return 0u;
    const uint8_t capacity = static_cast<uint8_t>(sizeof(irq_fifo_));
    const uint8_t bit = irq_fifo_[irq_fifo_head_];
    irq_fifo_[irq_fifo_head_] = 0u;
    irq_fifo_head_ = static_cast<uint8_t>((irq_fifo_head_ + 1u) % capacity);
    --irq_fifo_count_;
    block_queued_ = irq_fifo_count_ != 0u;
    return bit;
}

uint8_t SiemensMp377Sm501AudioOutput::QueuedBlocks() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return irq_fifo_count_;
}

void SiemensMp377Sm501AudioOutput::ClearQueue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    irq_fifo_head_ = 0u;
    irq_fifo_count_ = 0u;
    block_queued_ = false;
    id_pending_consume_.store(false);
    for (uint8_t& bit : irq_fifo_)
        bit = 0u;
}

std::chrono::microseconds SiemensMp377Sm501AudioOutput::BlockPeriod() const {
    constexpr uint64_t usec =
        (static_cast<uint64_t>(kFramesPerFirmwareHalf) * 1000000ull + kDefaultRateHz / 2u) / kDefaultRateHz;
    return std::chrono::microseconds(usec);
}

void SiemensMp377Sm501AudioOutput::StartPacer() {
    std::lock_guard<std::mutex> lock(pacer_mutex_);
    if (pacer_thread_.joinable()) return;
    pacer_stop_ = false;
    pacer_enabled_ = false;
    pacer_thread_ = std::thread([this]() { PacerLoop(); });
}

void SiemensMp377Sm501AudioOutput::StopPacer() {
    {
        std::lock_guard<std::mutex> lock(pacer_mutex_);
        pacer_enabled_ = false;
        pacer_stop_ = true;
    }
    pacer_cv_.notify_all();
    if (pacer_thread_.joinable()) pacer_thread_.join();
}

void SiemensMp377Sm501AudioOutput::SetPacerEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(pacer_mutex_);
        pacer_enabled_ = enabled;
    }
    pacer_cv_.notify_all();
}

void SiemensMp377Sm501AudioOutput::PacerLoop() {
    auto& freeze = emu_.Get<EmulationFreeze>();
    const MMRESULT timer_period = timeBeginPeriod(1u);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == nullptr) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    auto wait_until = [timer](std::chrono::steady_clock::time_point deadline) {
        const auto now = std::chrono::steady_clock::now();
        if (deadline <= now) return;
        if (timer == nullptr) {
            std::this_thread::sleep_until(deadline);
            return;
        }
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
        const LONGLONG ticks = std::max<LONGLONG>(1, (ns + 99) / 100);
        LARGE_INTEGER due{};
        due.QuadPart = -ticks;
        if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            std::this_thread::sleep_until(deadline);
            return;
        }
        WaitForSingleObject(timer, INFINITE);
    };
    std::unique_lock<std::mutex> lock(pacer_mutex_);
    while (!pacer_stop_) {
        pacer_cv_.wait(lock, [this]() { return pacer_stop_ || pacer_enabled_; });
        if (pacer_stop_) break;
        auto next = std::chrono::steady_clock::now() + BlockPeriod();
        while (pacer_enabled_ && !pacer_stop_) {
            lock.unlock();
            wait_until(next);
            lock.lock();
            if (!pacer_enabled_ || pacer_stop_) break;
            lock.unlock();
            {
                auto frozen = freeze.WorkerSection();
                ServiceClockTick();
            }
            lock.lock();
            const auto period = BlockPeriod();
            next += period;
            const auto now = std::chrono::steady_clock::now();
            if (next <= now) next = now + period;
        }
    }
    lock.unlock();
    if (timer != nullptr) {
        CancelWaitableTimer(timer);
        CloseHandle(timer);
    }
    if (timer_period == TIMERR_NOERROR) timeEndPeriod(1u);
}

void SiemensMp377Sm501AudioOutput::ServiceClockTick() {
    if (active_) ServiceCompletions();
}

void SiemensMp377Sm501AudioOutput::ServiceCompletions() {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    if (!active_) return;
    if ((regs.regs_[kSm501IrqStatusReg / 4u] & SiemensMp377Sm501AudioMcu::kOutputIrqBit) != 0u) return;
    if (emu_.Get<SiemensMp377Sm501AudioMcu>().OutputTokenPending()) return;
    if (id_pending_consume_.load()) return;
    const uint8_t bit = PopIrqBit();
    if (bit != 0u) RaiseIrq(bit);
}

void SiemensMp377Sm501AudioOutput::RaiseIrq(uint8_t buffer_bit) {
    buffer_bit &= 0x03u;
    if (buffer_bit == 0u) return;
    id_pending_consume_.store(true);
    next_irq_bit_ = buffer_bit == 1u ? 2u : 1u;
    emu_.Get<SiemensMp377Sm501AudioMcu>().RaiseOutputIrq(buffer_bit);
}

REGISTER_SERVICE(SiemensMp377Sm501AudioOutput);

} // namespace siemens_mp377
