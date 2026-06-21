#define NOMINMAX

#include "sa11xx_dma_capture.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../state/emulation_freeze.h"
#include "../../host/audio_activity_widget.h"

#include <vector>

namespace {
constexpr UINT kMsgEnsureOpen = WM_USER + 0x10u;
}  /* namespace */

void Sa11xxDmaCapture::OnReady() {
    cfg_ = AudioConfig();
    sink_.Start(nullptr,
                [this](const MSG& msg) { OnThreadMessage(msg); },
                cfg_.log_tag);
    emu_.Get<Sa11xxDma>().RegisterSink(
        [this](const Sa11xxDma::ChannelState& st) { return OnDmaStart(st); });
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void Sa11xxDmaCapture::OnShutdown() {
    sink_.Stop();
}

bool Sa11xxDmaCapture::OnDmaStart(const Sa11xxDma::ChannelState& st) {
    if ((st.ddar & cfg_.ddar_mask) != cfg_.ddar_value) return false;

    /* No host capture device: decline the receive page. Sa11xxDma then leaves
       DONE unset and the wavedev IST blocks - the faithful "receive with no
       incoming data" state, never a fake completion. */
    if (live_.load(std::memory_order_acquire) == kDead) return false;

    const uint32_t dst_pa = st.buffer_b ? st.dbsb : st.dbsa;
    const uint32_t bytes  = st.buffer_b ? st.dbtb : st.dbta;
    if (bytes == 0 || bytes > cfg_.max_page_bytes) return false;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_.push_back({st.channel_index, st.buffer_b, dst_pa, bytes});
    }
    if (!open_posted_.exchange(true)) sink_.Post(kMsgEnsureOpen);
    return true;
}

void Sa11xxDmaCapture::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgEnsureOpen) {
        EnsureOpenOnThread();
    } else if (msg.message == MM_WIM_DATA) {
        auto* hdr = reinterpret_cast<LPWAVEHDR>(msg.lParam);
        if (!hdr) return;
        OnRecordedData(reinterpret_cast<const uint8_t*>(hdr->lpData),
                       hdr->dwBytesRecorded);
        sink_.Requeue(hdr);
    }
}

void Sa11xxDmaCapture::EnsureOpenOnThread() {
    if (sink_.EnsureFormat(SampleRateHz(), cfg_.channels, cfg_.bits_per_sample)) {
        live_.store(kLive, std::memory_order_release);
        return;
    }

    /* Open failed: no host capture device. Leave the optimistically-claimed
       receive pages uncompleted and decline future ones (OnDmaStart) so DONE
       stays unset and the wavedev IST blocks - the faithful idle-receive state,
       never a manufactured completion. */
    live_.store(kDead, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_.clear();
        fifo_.clear();
    }
    LOG(Caution, "[%s] microphone capture unavailable (host waveIn open failed); "
                 "guest record will not progress\n", cfg_.log_tag);
}

void Sa11xxDmaCapture::OnRecordedData(const uint8_t* data, uint32_t bytes) {
    if (bytes != 0) {
        std::lock_guard<std::mutex> lk(mtx_);
        fifo_.insert(fifo_.end(), data, data + bytes);
        /* Bound latency when the guest stops draining but the mic keeps
           streaming: drop the oldest beyond four max-size pages. */
        const size_t cap = static_cast<size_t>(cfg_.max_page_bytes) * 4u;
        if (fifo_.size() > cap) fifo_.erase(fifo_.begin(), fifo_.end() - cap);
    }
    DrainPending();
}

void Sa11xxDmaCapture::DrainPending() {
    for (;;) {
        PendingPage p{};
        std::vector<uint8_t> buf;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (pending_.empty()) return;
            p = pending_.front();
            if (fifo_.size() < p.byte_count) return;
            buf.assign(fifo_.begin(), fifo_.begin() + p.byte_count);
            fifo_.erase(fifo_.begin(), fifo_.begin() + p.byte_count);
            pending_.pop_front();
        }
        WriteAndComplete(p, buf.data());
    }
}

void Sa11xxDmaCapture::WriteAndComplete(const PendingPage& p, const uint8_t* data) {
    /* Freeze against a hibernation snapshot: this audio thread writes guest DRAM
       and mutates the DMA's guest-visible DONE/IRQ state. Held only around the
       guest-state touch, never across a wait. */
    {
        auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
        emu_.Get<EmulatedMemory>().CopyIn(p.dst_pa, data, p.byte_count);
        emu_.Get<Sa11xxDma>().CompleteTransfer(p.channel, p.buffer_b);
    }
    emu_.Get<AudioActivityWidget>().MarkRx();
    LOG(Periph, "[%s] capture DONE ch=%u buf=%c bytes=%u\n",
        cfg_.log_tag, p.channel, p.buffer_b ? 'B' : 'A', p.byte_count);
}
