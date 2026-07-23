#pragma once

#include "../../host/paced_wave_out.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

class CerfEmulator;
class StateWriter;
class StateReader;

/* wavedev.dll audio DMA block, companion 0x0880-0x08CC (base dword_F640C0 =
   0x0A000000). */
class CasioCassiopeiaEm500Audio {
public:
    void Init(CerfEmulator& emu, std::function<void()> on_irq_change);
    void OnShutdown();

    bool TryReadHalf (uint32_t off, uint16_t& out);
    bool TryWriteHalf(uint32_t off, uint16_t  value);
    bool TryReadWord (uint32_t off, uint32_t& out);
    bool TryWriteWord(uint32_t off, uint32_t  value);

    /* Codec register 5 bit7 = the loc_F62984 rate doubler: @0xF62A20 sll $v1,$a1,7 /
       @0xF62A24 or 0xD007 / @0xF62A26 sw 0x3C0. */
    void SetRateDoubler(bool on);

    bool IrqPending() const {
        return status_8A8_.load(std::memory_order_acquire) != 0u;
    }

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    /* dword_F622D4 @0xF622E4 lhu 0x8A8, the only instruction accessing 0x8A8 in
       wavedev.dll and nk_main_kernel.exe; bit0 selects the per-block class-0x10
       branch, bit1 the class-0x20 branch. */
    static constexpr uint16_t kStatusBlockDone = 0x1u;

    /* sub_F615D8 @0xF615E4/@0xF615EE/@0xF615F8/@0xF61602 programs 0x8B0/0x8B4/
       0x8B8/0x8BC; sub_F61614 @0xF61624/@0xF6162E writes 0x8B8/0x8BC only. */
    static constexpr uint32_t kDescCurStart = 0;
    static constexpr uint32_t kDescCurEnd   = 1;
    static constexpr uint32_t kDescNextStart = 2;
    static constexpr uint32_t kDescNextEnd   = 3;

    /* loc_F62984 @0xF62A98 sw 1 -> 0x08A0, the last write of the start path;
       loc_F61EAC @0xF61EBC and sub_F62520 @0xF6252E write 0. */
    bool Running() const { return (reg_8A0_ & 0x1u) != 0u; }
    /* loc_F618CC @0xF6194E 0x0890 |= 2; loc_F61EAC @0xF61F0C li 3 / neg / and. */
    bool PlayEnabled() const { return (reg_890_ & 0x2u) != 0u; }
    /* loc_F618CC @0xF61900 lw 0x1C($s0) = the loc_F61998 converter selector read
       @0xF619CA: {2,3} -> @0xF61924 and 0xFFFD, else @0xF61914 or 2. Cases 2/3
       (@0xF61B6E, @0xF61A9A) convert two samples per iteration scaled by two
       different volume registers; cases 0/1 (@0xF61CDC, @0xF61C34) one. */
    uint16_t Channels() const { return (reg_888_ & 0x2u) != 0u ? 1u : 2u; }

    void OnLatchWrite(uint32_t value, uint32_t keep_mask);
    void OnChannelWrite(uint32_t value);
    void StartSink();
    void StartTransfers(bool holds_snapshot);
    void QueueDescriptor(uint32_t start_index, bool holds_snapshot);
    void OnBlockDone(uint32_t sink_gen);

    CerfEmulator* emu_ = nullptr;
    std::function<void()> on_irq_change_;
    PacedWaveOut paced_;

    mutable std::mutex mtx_;

    uint32_t reg_880_ = 0;
    uint32_t reg_884_ = 0;
    uint32_t reg_888_ = 0;
    uint32_t reg_890_ = 0;
    uint32_t reg_898_ = 0;
    uint32_t reg_8A0_ = 0;
    uint32_t desc_[4] = {};
    uint32_t reg_8C4_ = 0;
    uint32_t reg_8C8_ = 0;
    uint32_t reg_8CC_ = 0;
    bool     rate_doubler_ = false;

    std::atomic<uint16_t> status_8A8_{0};

    /* wavedev.dll loc_F618CC @0xF61930 jal loc_F61998 ($a1=0) / @0xF61938 ($a1=1);
       sub_F615D8 @0xF615E4 0x8B0 / @0xF615EE 0x8B4 / @0xF615F8 0x8B8 / @0xF61602
       0x8BC; sub_F61614 @0xF61624 0x8B8 / @0xF6162E 0x8BC. */
    static constexpr uint32_t kMaxQueued = 2;
    uint32_t queued_      = 0;
    bool     next_queued_ = false;
    uint32_t sink_gen_    = 0;
};
