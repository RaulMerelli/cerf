#define NOMINMAX

#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../../boards/board_detector.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/wave_out_sink.h"
#include "../../socs/sa11xx/sa11xx_dma.h"
#include "../../socs/sa11xx/sa11xx_mcp.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace {

/* SA-1100 MCP audio transmit DMA: DA[31:8]=0x818002 (MCDR0 port 0x80060008),
   DS=0xA — the runtime DDAR wavedev programs is 0x818002A8 (DW=1 halfword,
   RW=0 write). The dev-man Table 11-6 text export misaligns the DS column. */
constexpr uint32_t kMcpAudioTxDdarMask  = 0xFFFFFFF0u;
constexpr uint32_t kMcpAudioTxDdarValue = 0x818002A0u;

/* SA-1100 MCP audio is mono (subframe 0); samples are 12-bit codec data
   left-justified in a 16-bit FIFO entry, so the DRAM buffer is 16-bit mono PCM. */
constexpr uint16_t kChannels      = 1u;
constexpr uint16_t kBitsPerSample = 16u;
constexpr uint32_t kMaxPageBytes  = 0x2000u;   /* wavedev uses 0x1000 buffers. */
constexpr uint32_t kPagesQueued   = 4u;

constexpr UINT kMsgSubmitPage = WM_USER + 0x10u;

struct PendingPage {
    uint32_t dma_channel;
    bool     buffer_b;
    uint32_t src_pa;
    uint32_t byte_count;
    uint32_t rate_hz;
};

class Jornada820AudioPlayer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

    void OnReady() override {
        for (uint32_t i = 0; i < kPagesQueued; ++i) slots_[i].bytes.resize(kMaxPageBytes);
        sink_.Start(nullptr,
                    [this](const MSG& msg) { OnThreadMessage(msg); },
                    "J820Audio");
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

    WaveOutSink             sink_;
    std::mutex              slots_mtx_;
    Slot                    slots_[kPagesQueued];
    uint32_t                next_slot_ = 0;
    std::deque<PendingPage> page_queue_;

    bool OnDmaStart(const Sa11xxDma::ChannelState& st) {
        if ((st.ddar & kMcpAudioTxDdarMask) != kMcpAudioTxDdarValue) return false;
        const uint32_t src_pa = st.buffer_b ? st.dbsb : st.dbsa;
        const uint32_t bytes  = st.buffer_b ? st.dbtb : st.dbta;
        if (bytes == 0 || bytes > kMaxPageBytes) return false;

        auto* pending = new PendingPage{
            st.channel_index, st.buffer_b, src_pa, bytes,
            emu_.Get<Sa11xxMcp>().GetAudioSampleRateHz(),
        };
        sink_.Post(kMsgSubmitPage, 0, reinterpret_cast<LPARAM>(pending));
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
        sink_.EnsureFormat(p.rate_hz, kChannels, kBitsPerSample,
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

        emu_.Get<Sa11xxDma>().CompleteTransfer(completed_ch, completed_buf);
        if (have_next) LoadIntoSlot(*slot, next_page);
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada820AudioPlayer);
