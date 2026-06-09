#include "pxa255_ac97.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/ac97_codec.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <algorithm>
#include <cstring>

REGISTER_SERVICE(Pxa255Ac97);

bool Pxa255Ac97::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetSoc() == SocFamily::PXA25x;
}

/* Stop the audio thread before any peer its completion callback re-enters is
   destroyed. */
void Pxa255Ac97::OnShutdown() { sink_.Stop(); }

void Pxa255Ac97::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
    sink_.Start(
        [this] {
            sink_.EnsureFormat(kSampleRate, kChannels, kBitsPerSamp,
                               /*allow_resampler=*/false, /*busy=*/false);
        },
        [this](const MSG& msg) { OnThreadMessage(msg); },
        "Pxa255Ac97");
}

void Pxa255Ac97::OnThreadMessage(const MSG& msg) {
    if (msg.message != MM_WOM_DONE) return;
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        if (msg.lParam != 0 && sink_.IsOpen()) {
            sink_.Unprepare(reinterpret_cast<LPWAVEHDR>(msg.lParam));
        }
        if (output_active_.load(std::memory_order_acquire)) cb = on_block_done_;
    }
    /* Invoke without the lock: the callback re-enters QueueOutput (DMA ->
       AC97 lock order); holding audio_mutex_ here would self-deadlock. */
    if (cb) cb();
}

void Pxa255Ac97::BeginAudioOut(std::function<void()> on_block_done) {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    on_block_done_ = std::move(on_block_done);
    output_active_.store(true, std::memory_order_release);
}

void Pxa255Ac97::StopAudioOut() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    output_active_.store(false, std::memory_order_release);
    on_block_done_ = nullptr;
    sink_.Reset();   /* flush queued buffers. */
}

void Pxa255Ac97::QueueOutput(const void* host_bytes, uint32_t length) {
    if (length == 0) return;
    if (length > kMaxBlock) length = kMaxBlock;

    std::lock_guard<std::mutex> lk(audio_mutex_);
    if (!output_active_.load(std::memory_order_acquire)) return;

    /* Silent mode (or device error): pace by posting completion immediately so
       the DMA delivers the next block and the ring keeps cycling. */
    if (!sink_.IsOpen()) {
        sink_.Post(MM_WOM_DONE, 0, 0);
        return;
    }

    /* One block in flight: the prior block's MM_WOM_DONE freed header_ before the
       DMA paced this next call. If somehow still queued, post a synthetic
       completion so the ring keeps cycling instead of stalling. */
    if (header_.dwFlags & WHDR_INQUEUE) {
        sink_.Post(MM_WOM_DONE, 0, 0);
        return;
    }
    std::memset(&header_, 0, sizeof(header_));
    std::memcpy(buffer_, host_bytes, length);
    header_.lpData         = reinterpret_cast<LPSTR>(buffer_);
    header_.dwBufferLength = length;

    if (!sink_.Play(&header_)) {
        sink_.Post(MM_WOM_DONE, 0, 0);
    }
}

uint16_t Pxa255Ac97::CodecRead(uint32_t reg) {
    if (auto* codec = emu_.TryGet<Ac97Codec>()) return codec->ReadReg(reg);
    return codec_[reg];
}

void Pxa255Ac97::CodecWrite(uint32_t reg, uint16_t value) {
    if (auto* codec = emu_.TryGet<Ac97Codec>()) { codec->WriteReg(reg, value); return; }
    codec_[reg] = value;
}

uint32_t Pxa255Ac97::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (InCodecWindow(off)) return CodecRead((off - kCodecBase) >> 1);
    switch (off) {
        case kMODR: { uint16_t w = 0; return PopTouchWords(&w, 1u) ? w : 0u; }
        case kMISR: {
            std::lock_guard<std::mutex> lk(touch_mutex_);
            return touch_fifo_.empty() ? 0u : kMisrFsr;
        }
        case kPOCR: return pocr_;
        case kPICR: return picr_;
        case kMCCR: return mccr_;
        case kGCR:  return gcr_;
        case kMOCR: return mocr_;
        case kMICR: return micr_;
        case kGSR:  return kGsrReady;  /* codec ready, commands done. */
        case kCAR:  return 0u;         /* §13.8.3.7 Table 13-13: CAIP=0 -> AC-link free. */
        default:    return 0u;         /* status / codec / FIFO idle. */
    }
}

void Pxa255Ac97::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (InCodecWindow(off)) {
        CodecWrite((off - kCodecBase) >> 1, static_cast<uint16_t>(value));
        return;
    }
    switch (off) {
        case kPOCR: pocr_ = value; return;
        case kPICR: picr_ = value; return;
        case kMCCR: mccr_ = value; return;
        case kGCR:  gcr_  = value; return;
        case kMOCR: mocr_ = value; return;
        case kMICR: micr_ = value; return;
        /* §13.8.3.7 Table 13-13: CAR.CAIP is HW-owned and no codec is modeled, so
           CAR always reads CAIP=0 (free). Storing a write would let a stale CAIP=1
           read back BUSY and spin the driver's CODEC-access poll (§13.6.3). */
        case kCAR:  return;
        default:    return;            /* GSR W1C status / FIFO writes. */
    }
}

uint16_t Pxa255Ac97::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (InCodecWindow(off)) return CodecRead((off - kCodecBase) >> 1);
    const uint32_t word  = ReadWord(addr & ~0x3u);
    const uint32_t shift = (off & 0x2u) * 8u;
    return static_cast<uint16_t>((word >> shift) & 0xFFFFu);
}

void Pxa255Ac97::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    if (InCodecWindow(off)) {
        CodecWrite((off - kCodecBase) >> 1, value);
        return;
    }
    const uint32_t shift = (off & 0x2u) * 8u;
    const uint32_t word  = ReadWord(addr & ~0x3u);
    WriteWord(addr & ~0x3u,
              (word & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift));
}

void Pxa255Ac97::PushTouchSample(const uint16_t* words, uint32_t count) {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(touch_mutex_);
        for (uint32_t i = 0; i < count; ++i) touch_fifo_.push_back(words[i]);
        cb = on_touch_sample_;
    }
    /* Invoke without the lock: the DMA callback re-enters PopTouchWords. */
    if (cb) cb();
}

void Pxa255Ac97::BeginTouchCapture(std::function<void()> on_sample) {
    std::lock_guard<std::mutex> lk(touch_mutex_);
    on_touch_sample_ = std::move(on_sample);
}

void Pxa255Ac97::StopTouchCapture() {
    std::lock_guard<std::mutex> lk(touch_mutex_);
    on_touch_sample_ = nullptr;
    touch_fifo_.clear();
}

uint32_t Pxa255Ac97::PopTouchWords(uint16_t* out, uint32_t max) {
    std::lock_guard<std::mutex> lk(touch_mutex_);
    const uint32_t n = std::min<uint32_t>(max, static_cast<uint32_t>(touch_fifo_.size()));
    for (uint32_t i = 0; i < n; ++i) out[i] = touch_fifo_[i];
    touch_fifo_.erase(touch_fifo_.begin(), touch_fifo_.begin() + n);
    return n;
}

uint32_t Pxa255Ac97::TouchFifoCount() {
    std::lock_guard<std::mutex> lk(touch_mutex_);
    return static_cast<uint32_t>(touch_fifo_.size());
}
