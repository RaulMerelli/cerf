#define NOMINMAX

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/service.h"
#include "../../boards/board_detector.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/intel_sa1111/sa1111_sac.h"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr uint16_t kChannels      = 2u;
constexpr uint16_t kBitsPerSample = 16u;   /* §7.3.1: L in bits 15:0, R in
                                              31:16 — WAV interleave order. */
constexpr uint32_t kMaxPageBytes  = 65536u;
constexpr uint32_t kPagesQueued   = 4u;

constexpr UINT kMsgSubmitPage = WM_USER + 0x10u;

class Jornada720AudioPlayer : public Service {
public:
    using Service::Service;
    ~Jornada720AudioPlayer() override {
        shutdown_.store(true, std::memory_order_release);
        if (audio_thread_id_) {
            PostThreadMessageW(audio_thread_id_, WM_QUIT, 0, 0);
        }
        if (audio_thread_.joinable()) audio_thread_.join();
        if (out_device_)         waveOutClose(out_device_);
        if (thread_ready_event_) CloseHandle(thread_ready_event_);
    }

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }

    void OnReady() override {
        for (uint32_t i = 0; i < kPagesQueued; ++i) {
            slots_[i].bytes.resize(kMaxPageBytes);
        }
        thread_ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        audio_thread_ = std::thread([this] { AudioThreadMain(); });
        WaitForSingleObject(thread_ready_event_, INFINITE);

        emu_.Get<Sa1111Sac>().RegisterTransmitSink(
            [this](const Sa1111Sac::TransmitPage& page) {
                return OnPage(page);
            });
    }

private:
    struct Slot {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
        bool                 buffer_b  = false;
        bool                 in_flight = false;
    };

    HWAVEOUT          out_device_         = nullptr;
    uint32_t          open_rate_hz_       = 0;
    DWORD             audio_thread_id_    = 0;
    HANDLE            thread_ready_event_ = nullptr;
    std::thread       audio_thread_;
    std::atomic<bool> shutdown_{false};

    std::mutex slots_mtx_;
    Slot       slots_[kPagesQueued];
    uint32_t   next_slot_ = 0;
    std::deque<Sa1111Sac::TransmitPage> page_queue_;

    bool OnPage(const Sa1111Sac::TransmitPage& page) {
        if (page.byte_count == 0 || page.byte_count > kMaxPageBytes)
            return false;
        auto* pending = new Sa1111Sac::TransmitPage(page);
        PostThreadMessageW(audio_thread_id_, kMsgSubmitPage,
                           0, reinterpret_cast<LPARAM>(pending));
        return true;
    }

    void AudioThreadMain() {
        audio_thread_id_ = GetCurrentThreadId();
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        SetEvent(thread_ready_event_);

        while (!shutdown_.load(std::memory_order_acquire) &&
               GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == kMsgSubmitPage) {
                auto* p = reinterpret_cast<Sa1111Sac::TransmitPage*>(msg.lParam);
                SubmitPage(*p);
                delete p;
                continue;
            }
            if (msg.message == MM_WOM_DONE) {
                OnPageDone(reinterpret_cast<LPWAVEHDR>(msg.lParam));
                continue;
            }
        }
    }

    void EnsureWaveOutFor(uint32_t rate_hz) {
        if (out_device_ && open_rate_hz_ == rate_hz) return;
        if (out_device_) {
            bool busy;
            {
                std::lock_guard<std::mutex> lk(slots_mtx_);
                busy = false;
                for (auto& s : slots_) busy |= s.in_flight;
            }
            if (busy) return;          /* rate switch lands on next idle page. */
            waveOutClose(out_device_);
            out_device_ = nullptr;
        }
        WAVEFORMATEX fmt{};
        fmt.wFormatTag      = WAVE_FORMAT_PCM;
        fmt.nChannels       = kChannels;
        fmt.nSamplesPerSec  = rate_hz;
        fmt.wBitsPerSample  = kBitsPerSample;
        fmt.nBlockAlign     = static_cast<uint16_t>(
                                (fmt.wBitsPerSample / 8) * fmt.nChannels);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

        const MMRESULT r = waveOutOpen(&out_device_, WAVE_MAPPER, &fmt,
                                       audio_thread_id_, 0, CALLBACK_THREAD);
        if (r != MMSYSERR_NOERROR) {
            LOG(Caution, "Jornada720AudioPlayer: waveOutOpen(%u Hz) failed "
                "mmresult=%u\n", rate_hz, r);
            out_device_ = nullptr;
        } else {
            open_rate_hz_ = rate_hz;
            LOG(Periph, "[J720Audio] waveOut opened %u Hz x %u ch x %u bit\n",
                rate_hz, kChannels, kBitsPerSample);
        }
    }

    Slot* AllocSlotLocked() {
        for (uint32_t tries = 0; tries < kPagesQueued; ++tries) {
            const uint32_t idx = (next_slot_ + tries) % kPagesQueued;
            if (!slots_[idx].in_flight) {
                next_slot_ = (idx + 1) % kPagesQueued;
                return &slots_[idx];
            }
        }
        return nullptr;
    }

    void SubmitPage(const Sa1111Sac::TransmitPage& p) {
        EnsureWaveOutFor(p.sample_rate_hz);
        if (!out_device_) {
            /* No host audio device: the guest still needs done IRQs at the
               real cadence of the buffer (bytes / 4 frames at fs), or the
               ping-pong storms interrupts at host speed. This thread's only
               job is that pacing. */
            const uint32_t ms = (uint32_t)((uint64_t)p.byte_count * 1000u /
                                           ((uint64_t)p.sample_rate_hz * 4u));
            Sleep(ms ? ms : 1u);
            emu_.Get<Sa1111Sac>().CompleteTransmit(p.buffer_b);
            return;
        }
        Slot* slot;
        {
            std::lock_guard<std::mutex> lk(slots_mtx_);
            slot = AllocSlotLocked();
            if (!slot) {
                page_queue_.push_back(p);
                return;
            }
        }
        LoadIntoSlot(*slot, p);
    }

    void LoadIntoSlot(Slot& slot, const Sa1111Sac::TransmitPage& p) {
        auto& mem = emu_.Get<EmulatedMemory>();
        for (uint32_t i = 0; i < p.byte_count; ++i) {
            slot.bytes[i] = mem.ReadByte(p.src_pa + i);
        }
        slot.buffer_b = p.buffer_b;

        std::memset(&slot.hdr, 0, sizeof(slot.hdr));
        slot.hdr.lpData         = reinterpret_cast<LPSTR>(slot.bytes.data());
        slot.hdr.dwBufferLength = p.byte_count;
        slot.hdr.dwUser         = reinterpret_cast<DWORD_PTR>(&slot);

        MMRESULT r = waveOutPrepareHeader(out_device_, &slot.hdr,
                                          sizeof(WAVEHDR));
        if (r != MMSYSERR_NOERROR) {
            LOG(Caution, "Jornada720AudioPlayer: waveOutPrepareHeader "
                "failed %u\n", r);
            emu_.Get<Sa1111Sac>().CompleteTransmit(p.buffer_b);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(slots_mtx_);
            slot.in_flight = true;
        }
        r = waveOutWrite(out_device_, &slot.hdr, sizeof(WAVEHDR));
        if (r != MMSYSERR_NOERROR) {
            LOG(Caution, "Jornada720AudioPlayer: waveOutWrite failed %u\n", r);
            waveOutUnprepareHeader(out_device_, &slot.hdr, sizeof(WAVEHDR));
            {
                std::lock_guard<std::mutex> lk(slots_mtx_);
                slot.in_flight = false;
            }
            emu_.Get<Sa1111Sac>().CompleteTransmit(p.buffer_b);
        }
    }

    void OnPageDone(LPWAVEHDR hdr) {
        if (!hdr || !out_device_) return;
        auto* slot = reinterpret_cast<Slot*>(hdr->dwUser);
        waveOutUnprepareHeader(out_device_, &slot->hdr, sizeof(WAVEHDR));

        const bool completed_buf = slot->buffer_b;
        Sa1111Sac::TransmitPage next_page{};
        bool have_next = false;
        {
            std::lock_guard<std::mutex> lk(slots_mtx_);
            slot->in_flight = false;
            if (!page_queue_.empty()) {
                next_page = page_queue_.front();
                page_queue_.pop_front();
                have_next = true;
            }
        }

        emu_.Get<Sa1111Sac>().CompleteTransmit(completed_buf);
        if (have_next) LoadIntoSlot(*slot, next_page);
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada720AudioPlayer);
