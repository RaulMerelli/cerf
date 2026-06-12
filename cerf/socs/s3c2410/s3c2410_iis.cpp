#define NOMINMAX

#include "s3c2410_iis.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../irq_controller.h"

#include <cstring>

REGISTER_SERVICE(S3C2410Iis);

namespace {

constexpr uint32_t kRegIISCON  = 0x00u;
constexpr uint32_t kRegIISMOD  = 0x04u;
constexpr uint32_t kRegIISPSR  = 0x08u;
constexpr uint32_t kRegIISFCON = 0x0Cu;
constexpr uint32_t kRegIISFIFO = 0x10u;

constexpr uint32_t kIisconTxFifoReady = 0x80u;
constexpr uint32_t kIisconRxFifoReady = 0x40u;

constexpr int kIrqDma2 = 19;   /* INT_DMA2 = SRCPND bit 19 — wavedev's ISR target. */

/* Posted on DMA enable: MM_WOM_DONE generates INT_DMA2 only past this point,
   since DMA was off when earlier messages queued. See BSP IOIIS::SetOutputDMA. */
constexpr UINT kMsgOutDmaEnable = 0xC001u;

}  /* namespace */

bool S3C2410Iis::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetSoc() == SocFamily::S3C2410;
}

/* Stop the audio thread before any peer it asserts INT_DMA2 into is destroyed. */
void S3C2410Iis::OnShutdown() { sink_.Stop(); }

void S3C2410Iis::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);

    /* Pin each WAVEHDR's buffer base once; Reset only zeroes the WAVEHDR fields. */
    out_headers_[0].lpData = reinterpret_cast<LPSTR>(&out_buffer_[0]);
    out_headers_[1].lpData = reinterpret_cast<LPSTR>(&out_buffer_[kBufferBytes]);
    curr_out_header_ = &out_headers_[0];

    sink_.Start(
        [this] {
            sink_.EnsureFormat(kSampleRate, kChannels, kBitsPerSamp,
                               /*allow_resampler=*/false, /*busy=*/false);
        },
        [this](const MSG& msg) { OnThreadMessage(msg); },
        "S3C2410Iis");
}

void S3C2410Iis::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgOutDmaEnable) {
        output_dma_enabled_.store(true, std::memory_order_release);
        return;
    }
    if (msg.message == MM_WOM_DONE) {
        auto* hdr = reinterpret_cast<LPWAVEHDR>(msg.lParam);
        /* wParam != 0 = real waveOut completion (handle); 0 = QueueOutput's
           manual pacing post, whose header is still owned by the ring. */
        if (msg.wParam != 0 && sink_.IsOpen() && hdr != nullptr) {
            sink_.Unprepare(hdr);
        }
        const bool manual_post = (msg.wParam == 0);
        const bool switching   = switch_out_queue_.load(std::memory_order_acquire);
        if (output_dma_enabled_.load(std::memory_order_acquire) &&
            (manual_post || switching)) {
            emu_.Get<IrqController>().AssertIrq(kIrqDma2);
        }
    }
}

bool S3C2410Iis::QueueSwitchPossible() const {
    const WAVEHDR* other = (curr_out_header_ == &out_headers_[0])
                           ? &out_headers_[1] : &out_headers_[0];
    return (other->dwFlags & WHDR_PREPARED) == 0;
}

void S3C2410Iis::SwitchQueue() {
    if (QueueSwitchPossible()) {
        curr_out_header_ = (curr_out_header_ == &out_headers_[0])
                           ? &out_headers_[1] : &out_headers_[0];
        ResetCurrentQueue();
    } else {
        switch_out_queue_.store(true, std::memory_order_release);
        curr_out_header_ = (curr_out_header_ == &out_headers_[0])
                           ? &out_headers_[1] : &out_headers_[0];
    }
}

void S3C2410Iis::ResetCurrentQueue() {
    WAVEHDR* hdr   = curr_out_header_;
    uint8_t* buf   = (hdr == &out_headers_[0]) ? &out_buffer_[0]
                                               : &out_buffer_[kBufferBytes];
    std::memset(hdr, 0, sizeof(*hdr));
    hdr->lpData = reinterpret_cast<LPSTR>(buf);
}

void S3C2410Iis::PlayCurrentQueue() {
    if (!sink_.IsOpen()) return;
    sink_.Play(curr_out_header_);
}

void S3C2410Iis::QueueOutput(const void* host_bytes, size_t length) {
    if (length != kBlockSize) {
        LOG(Caution, "S3C2410Iis::QueueOutput: length %zu != BLOCK_SIZE "
                "%u — BSP requires single-block writes\n",
                length, kBlockSize);
        return;
    }

    bool post_done = false;
    LPWAVEHDR done_hdr = nullptr;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        WAVEHDR* hdr = curr_out_header_;

        if (sink_.IsOpen()) {
            if (switch_out_queue_.load(std::memory_order_acquire)) {
                if ((hdr->dwFlags & WHDR_PREPARED) == 0) {
                    ResetCurrentQueue();
                    switch_out_queue_.store(false, std::memory_order_release);
                } else {
                    /* Drop the packet — CE got ahead of us, matches BSP. */
                    LOG(Periph, "[IIS] dropping audio packet — CE "
                            "outran host audio\n");
                    return;
                }
            }
            std::memcpy(reinterpret_cast<char*>(hdr->lpData) + hdr->dwBufferLength,
                        host_bytes, kBlockSize);
            hdr->dwBufferLength += kBlockSize;

            if (hdr->dwBufferLength == kBufferBytes) {
                PlayCurrentQueue();
                SwitchQueue();
            }

            if (!switch_out_queue_.load(std::memory_order_acquire)) {
                post_done = true;
                done_hdr  = hdr;
            }
        } else {
            /* Silent mode: still post MM_WOM_DONE so wavedev's ISR progresses. */
            post_done = true;
            done_hdr  = hdr;
        }
    }

    if (post_done) {
        sink_.Post(MM_WOM_DONE, 0, reinterpret_cast<LPARAM>(done_hdr));
    }
}

void S3C2410Iis::SetOutputDMA(bool on) {
    if (on) {
        sink_.Post(kMsgOutDmaEnable, 0, 0);
    } else {
        output_dma_enabled_.store(false, std::memory_order_release);
    }
}

uint32_t S3C2410Iis::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    uint32_t value = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (off) {
            case kRegIISCON:
                value = iiscon_ | kIisconTxFifoReady | kIisconRxFifoReady;
                break;
            case kRegIISMOD:  value = iismod_;  break;
            case kRegIISPSR:  value = iispsr_;  break;
            case kRegIISFCON: value = iisfcon_; break;
            case kRegIISFIFO: value = iisfifo_; break;
            default:
                HaltUnsupportedAccess("ReadWord", addr, 0);  /* noreturn */
        }
    }
    return value;
}

void S3C2410Iis::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (off) {
        case kRegIISCON:  iiscon_  = value; break;
        case kRegIISMOD:  iismod_  = value; break;
        case kRegIISPSR:  iispsr_  = value; break;
        case kRegIISFCON: iisfcon_ = value; break;
        case kRegIISFIFO: iisfifo_ = value; break;
        default:
            HaltUnsupportedAccess("WriteWord", addr, value);  /* noreturn */
    }
}

void S3C2410Iis::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    w.Write(iiscon_);
    w.Write(iismod_);
    w.Write(iispsr_);
    w.Write(iisfcon_);
    w.Write(iisfifo_);
    w.Write<uint8_t>(output_dma_enabled_.load(std::memory_order_acquire) ? 1u : 0u);
}

void S3C2410Iis::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    r.Read(iiscon_);
    r.Read(iismod_);
    r.Read(iispsr_);
    r.Read(iisfcon_);
    r.Read(iisfifo_);
    uint8_t dma = 0;
    r.Read(dma);
    output_dma_enabled_.store(dma != 0, std::memory_order_release);
}
