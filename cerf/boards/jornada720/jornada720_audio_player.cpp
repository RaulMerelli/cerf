#define NOMINMAX

#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../../boards/board_context.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/wave_out_sink.h"
#include "../../peripherals/intel_sa1111/sa1111_sac.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace {

constexpr uint16_t kChannels      = 2u;
constexpr uint16_t kBitsPerSample = 16u;   /* §7.3.1: L in 15:0, R in 31:16. */
constexpr uint32_t kMaxPageBytes  = 65536u;
constexpr uint32_t kPagesQueued   = 4u;

constexpr UINT kMsgSubmitPage = WM_USER + 0x10u;

class Jornada720AudioPlayer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }

    void OnReady() override {
        for (uint32_t i = 0; i < kPagesQueued; ++i) {
            slots_[i].bytes.resize(kMaxPageBytes);
        }
        sink_.Start(nullptr,
                    [this](const MSG& msg) { OnThreadMessage(msg); },
                    "J720Audio");
        emu_.Get<Sa1111Sac>().RegisterTransmitSink(
            [this](const Sa1111Sac::TransmitPage& page) { return OnPage(page); });
    }

private:
    struct Slot {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
        bool                 buffer_b  = false;
        bool                 in_flight = false;
    };

    WaveOutSink sink_;

    std::mutex slots_mtx_;
    Slot       slots_[kPagesQueued];
    uint32_t   next_slot_ = 0;
    std::deque<Sa1111Sac::TransmitPage> page_queue_;

    bool OnPage(const Sa1111Sac::TransmitPage& page) {
        if (page.byte_count == 0 || page.byte_count > kMaxPageBytes) return false;
        auto* pending = new Sa1111Sac::TransmitPage(page);
        sink_.Post(kMsgSubmitPage, 0, reinterpret_cast<LPARAM>(pending));
        return true;
    }

    void OnThreadMessage(const MSG& msg) {
        if (msg.message == kMsgSubmitPage) {
            auto* p = reinterpret_cast<Sa1111Sac::TransmitPage*>(msg.lParam);
            SubmitPage(*p);
            delete p;
        } else if (msg.message == MM_WOM_DONE) {
            OnPageDone(reinterpret_cast<LPWAVEHDR>(msg.lParam));
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
        bool busy = false;
        {
            std::lock_guard<std::mutex> lk(slots_mtx_);
            for (auto& s : slots_) busy |= s.in_flight;
        }
        sink_.EnsureFormat(p.sample_rate_hz, kChannels, kBitsPerSample,
                           /*allow_resampler=*/true, busy);

        if (!sink_.IsOpen()) {
            /* No host device: pace done IRQs at the real buffer cadence
               (bytes / 4 frames at fs) or the ping-pong storms at host speed. */
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

        {
            std::lock_guard<std::mutex> lk(slots_mtx_);
            slot.in_flight = true;
        }
        if (!sink_.Play(&slot.hdr)) {
            {
                std::lock_guard<std::mutex> lk(slots_mtx_);
                slot.in_flight = false;
            }
            emu_.Get<Sa1111Sac>().CompleteTransmit(p.buffer_b);
        }
    }

    void OnPageDone(LPWAVEHDR hdr) {
        if (!hdr || !sink_.IsOpen()) return;
        auto* slot = reinterpret_cast<Slot*>(hdr->dwUser);
        sink_.Unprepare(&slot->hdr);

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
