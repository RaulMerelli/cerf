#define NOMINMAX

#include "pr31x00_sib_audio.h"

#include "pr31x00_intc.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/audio_activity_widget.h"
#include "../../host/paced_wave_out.h"
#include "../../state/emulation_freeze.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace {

/* SNDSIZE selects a sound DMA buffer of at most 16 KB (TMPR3911 §13.6.1). */
constexpr uint32_t kMaxBytes = 0x4000u;

/* Interrupt Status 1 (§8.3.1): SND0_5INT<22> at the sound DMA halfway point,
   SND1_0INT<21> at end-of-buffer (§13.5). The wavedev IST (wavedev.dll
   sub_1872AD0, SYSINTR 13) blocks until one wakes it, so dropping them hangs audio. */
constexpr uint32_t kSndSet    = 0;   /* Interrupt Status 1 */
constexpr uint32_t kSnd0_5Int = 1u << 22;
constexpr uint32_t kSnd1_0Int = 1u << 21;

class Pr31x00SibAudioPlayer : public Pr31x00SibAudioSink {
public:
    using Pr31x00SibAudioSink::Pr31x00SibAudioSink;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::PR31500 || soc == SocFamily::PR31700;
    }

    void OnReady() override {
        paced_.Start("SibAudio", 0, /*channels=*/1, /*bits=*/16,
                     /*allow_resampler=*/false);
        emu_.Get<AudioActivityWidget>().NotePresent();
    }

    void OnShutdown() override { paced_.Stop(); }

    void StartSoundTx(uint32_t src_pa, uint32_t bytes, uint32_t rate_hz) override {
        if (bytes == 0 || bytes > kMaxBytes) return;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            src_pa_ = src_pa;
            bytes_  = bytes & ~1u;   /* whole 16-bit samples */
            half_   = 0;
            active_ = true;
        }
        paced_.SetFormat(rate_hz, /*channels=*/1, /*bits=*/16);
        paced_.BeginAudioOut([this] { OnBlockDone(); });
        QueueHalf();
    }

    void StopSoundTx() override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            active_ = false;
        }
        paced_.StopAudioOut();
    }

private:
    void QueueHalf() {
        uint32_t src_pa, bytes, h;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!active_) return;
            src_pa = src_pa_;
            bytes  = bytes_;
            h      = half_;
        }
        const uint32_t half = (bytes / 2u) & ~1u;
        if (half == 0u) return;
        const uint32_t off = h ? half : 0u;
        const uint32_t len = h ? ((bytes - half) & ~1u) : half;

        {
            auto  frozen = emu_.Get<EmulationFreeze>().WorkerSection();
            auto& mem    = emu_.Get<EmulatedMemory>();
            buf_.resize(len);
            /* MSB-first codec words (wavedev.dll sub_1871F50) -> LE host PCM. */
            for (uint32_t i = 0; i + 1u < len; i += 2u) {
                buf_[i]     = mem.ReadByte(src_pa + off + i + 1u);
                buf_[i + 1] = mem.ReadByte(src_pa + off + i);
            }
        }
        emu_.Get<AudioActivityWidget>().MarkTx();
        paced_.QueueOutput(buf_.data(), static_cast<uint32_t>(buf_.size()));
    }

    void OnBlockDone() {
        bool first;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!active_) return;
            first = (half_ == 0u);
        }
        /* Latch the boundary the pointer reached so the wavedev IST wakes and
           refills the finished half (§13.5). Freeze lock before the INTC mutex,
           per the peripheral contract (hibernation.md). */
        {
            auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
            emu_.Get<Pr31x00Intc>().SetPending(kSndSet, first ? kSnd0_5Int : kSnd1_0Int);
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!active_) return;
            half_ ^= 1u;
        }
        QueueHalf();
    }

    PacedWaveOut         paced_;
    std::mutex           mtx_;
    std::vector<uint8_t> buf_;
    uint32_t             src_pa_ = 0;
    uint32_t             bytes_  = 0;
    uint32_t             half_   = 0;
    bool                 active_ = false;
};

}  /* namespace */

REGISTER_SERVICE_AS(Pr31x00SibAudioPlayer, Pr31x00SibAudioSink);
