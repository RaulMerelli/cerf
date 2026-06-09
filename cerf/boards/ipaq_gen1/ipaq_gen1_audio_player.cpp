#define NOMINMAX

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/rate_probe.h"
#include "../../core/service.h"
#include "../../boards/board_detector.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/wave_out_sink.h"
#include "../../socs/sa11xx/sa11xx_dma.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace {

constexpr uint32_t kDdarSspTxMask  = 0xFFFFFF00u;
constexpr uint32_t kDdarSspTxValue = 0x81C01B00u;

constexpr uint32_t kSampleRate     = 22050u;
constexpr uint16_t kChannels       = 2u;
constexpr uint16_t kBitsPerSample  = 16u;
constexpr uint32_t kMaxPageBytes   = 16384u;
constexpr uint32_t kPagesQueued    = 4u;

constexpr UINT kMsgSubmitPage = WM_USER + 0x10u;

struct PendingPage {
    uint32_t dma_channel;
    bool     buffer_b;
    uint32_t src_pa;
    uint32_t byte_count;
};

class IpaqGen1AudioPlayer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::IpaqGen1;
    }

    void OnReady() override {
        for (uint32_t i = 0; i < kPagesQueued; ++i) {
            slots_[i].bytes.resize(kMaxPageBytes);
        }
        sink_.Start(nullptr,
                    [this](const MSG& msg) { OnThreadMessage(msg); },
                    "IpaqGen1Audio");
        emu_.Get<Sa11xxDma>().RegisterSink(
            [this](const Sa11xxDma::ChannelState& st) { return OnDmaStart(st); });
    }

private:
    struct Slot {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
        uint32_t             dma_channel = 0;
        bool                 buffer_b    = false;
        bool                 in_flight   = false;
    };

    WaveOutSink sink_;

    std::mutex              slots_mtx_;
    Slot                    slots_[kPagesQueued];
    uint32_t                next_slot_ = 0;
    std::deque<PendingPage> page_queue_;

    bool OnDmaStart(const Sa11xxDma::ChannelState& st) {
        if ((st.ddar & kDdarSspTxMask) != kDdarSspTxValue) return false;
        const uint32_t src_pa = st.buffer_b ? st.dbsb : st.dbsa;
        const uint32_t bytes  = st.buffer_b ? st.dbtb : st.dbta;
        if (bytes == 0) return false;
        if (bytes > kMaxPageBytes) return false;

        auto* pending = new PendingPage{
            st.channel_index, st.buffer_b, src_pa, bytes,
        };
        sink_.Post(kMsgSubmitPage, 0, reinterpret_cast<LPARAM>(pending));
#if CERF_DEV_MODE
        emu_.Get<RateProbe>().Inc(RateProbe::Counter::AudioMsgs);
#endif
        return true;
    }

    void OnThreadMessage(const MSG& msg) {
        if (msg.message == kMsgSubmitPage) {
            auto* p = reinterpret_cast<PendingPage*>(msg.lParam);
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

    void SubmitPage(const PendingPage& p) {
        sink_.EnsureFormat(kSampleRate, kChannels, kBitsPerSample,
                           /*allow_resampler=*/false, /*busy=*/false);
        if (!sink_.IsOpen()) {
            emu_.Get<Sa11xxDma>().CompleteTransfer(p.dma_channel, p.buffer_b);
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

    void LoadIntoSlot(Slot& slot, const PendingPage& p) {
        auto& mem = emu_.Get<EmulatedMemory>();
        for (uint32_t i = 0; i < p.byte_count; ++i) {
            slot.bytes[i] = mem.ReadByte(p.src_pa + i);
        }
        slot.dma_channel = p.dma_channel;
        slot.buffer_b    = p.buffer_b;

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
            emu_.Get<Sa11xxDma>().CompleteTransfer(p.dma_channel, p.buffer_b);
        }
    }

    void OnPageDone(LPWAVEHDR hdr) {
        if (!hdr || !sink_.IsOpen()) return;
        auto* slot = reinterpret_cast<Slot*>(hdr->dwUser);
        sink_.Unprepare(&slot->hdr);

        const uint32_t completed_ch  = slot->dma_channel;
        const bool     completed_buf = slot->buffer_b;
        PendingPage    next_page{};
        bool           have_next = false;
        {
            std::lock_guard<std::mutex> lk(slots_mtx_);
            slot->in_flight = false;
            if (!page_queue_.empty()) {
                next_page = page_queue_.front();
                page_queue_.pop_front();
                have_next = true;
            }
        }

        LOG(Periph, "[IpaqGen1AudioPlayer] waveOut DONE ch=%u buf=%c\n",
            completed_ch, completed_buf ? 'B' : 'A');
        emu_.Get<Sa11xxDma>().CompleteTransfer(completed_ch, completed_buf);

        if (have_next) LoadIntoSlot(*slot, next_page);
    }
};

}  /* namespace */

REGISTER_SERVICE(IpaqGen1AudioPlayer);
