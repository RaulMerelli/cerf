#pragma once

#include "../peripheral_base.h"

#include <cstdint>
#include <functional>
#include <mutex>

/* SA-1111 Serial Audio Controller (Developer's Manual ch.7, Table 7-31,
   base 0x40000600). SASR0/1 idle 0x9 = TNF|TFS — 0 wedges guest FIFO polls.
   TX DMA self-re-arms A/B: wavedev kicks SADTCS=0x53 ONCE (sub_FD17EC) and
   never rewrites it while playing; waiting for a re-kick stalls playback. */
class Sa1111Sac : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x40000600u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* One transmit buffer handed to the board audio player. src_pa points
       at guest DRAM (SADTSA/B), byte_count from SADTCA/B (4-byte units,
       Tables 7-20/7-21). Sink returns false to decline; the SAC then
       completes the page itself, unpaced. */
    struct TransmitPage {
        bool     buffer_b;
        uint32_t src_pa;
        uint32_t byte_count;
        uint32_t sample_rate_hz;
    };
    using TransmitSink = std::function<bool(const TransmitPage&)>;
    void RegisterTransmitSink(TransmitSink sink) { tx_sink_ = std::move(sink); }

    /* Called by the player when the page finished playing: sets TDBDA/B,
       raises INTC source 32/33 (Table 11-1) when TDIE, submits the other
       buffer while TDEN holds. */
    void CompleteTransmit(bool buffer_b);

private:
    enum : uint32_t {                  /* SADTCS bits, Table 7-19. */
        kTden  = 1u << 0,
        kTdie  = 1u << 1,
        kTdbda = 1u << 3,
        kTdsta = 1u << 4,
        kTdbdb = 1u << 5,
        kTdstb = 1u << 6,
        kTbiu  = 1u << 7,
    };

    void    WriteSadtcs(uint32_t value);
    void    TryStartNextLocked(std::unique_lock<std::mutex>& lk);

    std::mutex dma_mtx_;               /* JIT-thread writes vs player-thread
                                          completions. */
    TransmitSink tx_sink_;
    bool tx_running_  = false;
    bool tx_buffer_b_ = false;

    uint32_t sacr0_ = 0, sacr1_ = 0, sacr2_ = 0;
    uint32_t accar_ = 0, accdr_ = 0, acsar_ = 0;
    uint32_t sadtcs_ = 0, sadtsa_ = 0, sadtca_ = 0, sadtsb_ = 0, sadtcb_ = 0;
    uint32_t sadrcs_ = 0, sadrsa_ = 0, sadrca_ = 0, sadrsb_ = 0, sadrcb_ = 0;
    uint32_t saitr_ = 0;
    uint32_t l3car_ = 0;
    uint8_t  l3_regs_[256] = {};       /* UDA1344 codec settings via L3. */
    bool     l3wd_ = false, l3rd_ = false;
};
