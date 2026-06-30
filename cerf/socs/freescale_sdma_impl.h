#pragma once

#include "../peripherals/peripheral_base.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "../state/state_stream.h"
#include "freescale_sdma_bus.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

/* Shared core for the Freescale SDMA, same IP on i.MX31 (MCIMX31RM Ch 40) and
   i.MX51 (MCIMX51RM Ch 52) but with a divergent register layout. This core owns
   the same-offset registers + the shared logic; each concrete owns its divergent
   registers (ReadExtra/WriteExtra) and INTC line (AssertIrqLine). */
namespace cerf_freescale_sdma_detail {

constexpr uint32_t kSdmaSize = 0x00004000u;

/* Registers at the SAME offset on i.MX31 (Table 40-10) and i.MX51 (Table 52-9). */
constexpr uint32_t kOffMc0ptr      = 0x000u;
constexpr uint32_t kOffIntr        = 0x004u;
constexpr uint32_t kOffStopStat    = 0x008u;
constexpr uint32_t kOffHstart      = 0x00Cu;
constexpr uint32_t kOffEvtovr      = 0x010u;
constexpr uint32_t kOffDspovr      = 0x014u;
constexpr uint32_t kOffHostovr     = 0x018u;
constexpr uint32_t kOffEvtpend     = 0x01Cu;
constexpr uint32_t kOffReset       = 0x024u;
constexpr uint32_t kOffEvterr      = 0x028u;
constexpr uint32_t kOffIntrmask    = 0x02Cu;
constexpr uint32_t kOffPsw         = 0x030u;
constexpr uint32_t kOffEvterrdbg   = 0x034u;
constexpr uint32_t kOffConfig      = 0x038u;
constexpr uint32_t kOffOnceEnb     = 0x040u;
constexpr uint32_t kOffOnceData    = 0x044u;
constexpr uint32_t kOffOnceInstr   = 0x048u;
constexpr uint32_t kOffOnceStat    = 0x04Cu;
constexpr uint32_t kOffOnceCmd     = 0x050u;
constexpr uint32_t kOffIllinstaddr = 0x058u;
constexpr uint32_t kOffChn0addr    = 0x05Cu;
constexpr uint32_t kOffXtrigConf1  = 0x070u;
constexpr uint32_t kOffXtrigConf2  = 0x074u;
constexpr uint32_t kOffChnpriBase  = 0x100u;   /* CHNPRIn, n=0..31, both SoCs */
constexpr uint32_t kChannelCount   = 32u;

/* Reset values, identical on both SoCs (MCIMX31RM Table 40-10 / MCIMX51RM Table 52-9). */
constexpr uint32_t kResetDspovr      = 0xFFFFFFFFu;
constexpr uint32_t kResetConfig      = 0x00000003u;
constexpr uint32_t kResetOnceStat    = 0x0000E000u;
constexpr uint32_t kResetIllinstaddr = 0x00000001u;
constexpr uint32_t kResetChn0addr    = 0x00000050u;

/* RESET register bit 0 = SDMA software reset (self-clearing). */
constexpr uint32_t kResetBitReset = 1u << 0;

/* BD first-word bits + CCB layout, decompiled from CSPDDK: bit23 EXTD selects
   12-byte vs 8-byte BD stride, so a wrong stride walks garbage. */
constexpr uint32_t kBdDone       = 1u << 16;   /* word0 'D': SDMA owns (armed) (MCIMX51RM Table 52-96) */
constexpr uint32_t kBdWrap       = 1u << 17;   /* word0 'W': last BD, ring wraps to base (Table 52-96) */
constexpr uint32_t kBdIntr       = 1u << 19;   /* word0 'I': interrupt the AP on BD complete (Table 52-96) */
constexpr uint32_t kBdError      = 1u << 20;
constexpr uint32_t kBdExtd       = 1u << 23;
constexpr uint32_t kCcbStride    = 16u;
constexpr uint32_t kCcbBaseBdOff = 4u;
constexpr uint32_t kMaxBdWalk    = 256u;

template <uint32_t kBase, SocFamily kSoc>
class FreescaleSdmaBase : public Peripheral, public FreescaleSdmaBus {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == kSoc;
    }
    void OnReady() override {
        ResetCore();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSdmaSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        switch (off) {
            case kOffMc0ptr:      return mc0ptr_;
            case kOffIntr:
                return intr_;
            case kOffStopStat:    return stop_stat_;
            case kOffHstart:      return hstart_;
            case kOffEvtovr:      return evtovr_;
            case kOffDspovr:      return dspovr_;
            case kOffHostovr:     return hostovr_;
            case kOffEvtpend:     return evtpend_;
            case kOffReset:       return reset_;
            case kOffEvterr:      return evterr_;
            case kOffIntrmask:    return intrmask_;
            case kOffPsw:         return psw_;
            case kOffEvterrdbg:   return evterrdbg_;
            case kOffConfig:      return config_;
            case kOffOnceEnb:     return once_enb_;
            case kOffOnceData:    return once_data_;
            case kOffOnceInstr:   return once_instr_;
            case kOffOnceStat:    return once_stat_;
            case kOffOnceCmd:     return once_cmd_;
            case kOffIllinstaddr: return illinstaddr_;
            case kOffChn0addr:    return chn0addr_;
            case kOffXtrigConf1:  return xtrig_conf1_;
            case kOffXtrigConf2:  return xtrig_conf2_;
        }
        if (off >= kOffChnpriBase && off < kOffChnpriBase + kChannelCount * 4
            && (off & 0x3u) == 0) {
            return chnpri_[(off - kOffChnpriBase) / 4];
        }
        uint32_t out = 0;
        if (ReadExtra(off, out)) return out;
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        switch (off) {
            case kOffMc0ptr:    mc0ptr_ = value; return;
            /* INTR is per-bit w1c; the AP line is level-driven, so re-evaluate it
               or it re-fires forever. */
            case kOffIntr:
                intr_ &= ~value; RefreshIrq(); return;
            /* HSTART bit N starts channel N: signal completion of its BDs. */
            case kOffHstart:    if (value != 0) CompleteChannels(value); return;
            /* STOP_STAT is w1c: writing 1 to bit N clears HE[N]/HSTART[N]. CERF
               completes channels synchronously on HSTART, so this clears idle bits. */
            case kOffStopStat:  hstart_ &= ~value; stop_stat_ &= ~value; return;
            case kOffEvtovr:    evtovr_  = value; return;
            case kOffDspovr:    dspovr_  = value; return;
            case kOffHostovr:   hostovr_ = value; return;
            /* EVTPEND manually triggers a channel; silent accept would make a
               kernel-launched DMA read return zero. */
            case kOffEvtpend:
                if (value != 0) HaltUnsupportedAccess("WriteWord EVTPEND (manual channel trigger)", addr, value);
                return;
            case kOffReset:
                if (value & kResetBitReset) ResetCore();
                else                        reset_ = value & ~kResetBitReset;
                return;
            case kOffIntrmask:    intrmask_    = value; RefreshIrq(); return;
            case kOffConfig:      config_      = value; return;
            case kOffOnceEnb:     once_enb_    = value; return;
            case kOffOnceData:    once_data_   = value; return;
            case kOffOnceInstr:   once_instr_  = value; return;
            case kOffOnceCmd:     once_cmd_    = value; return;
            case kOffIllinstaddr: illinstaddr_ = value; return;
            case kOffChn0addr:    chn0addr_    = value; return;
            case kOffXtrigConf1:  xtrig_conf1_ = value; return;
            case kOffXtrigConf2:  xtrig_conf2_ = value; return;
        }
        if (off >= kOffChnpriBase && off < kOffChnpriBase + kChannelCount * 4
            && (off & 0x3u) == 0) {
            chnpri_[(off - kOffChnpriBase) / 4] = value;
            return;
        }
        if (WriteExtra(off, value)) return;
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write(mc0ptr_);    w.Write(intr_);      w.Write(stop_stat_);  w.Write(hstart_);
        w.Write(evtovr_);    w.Write(dspovr_);    w.Write(hostovr_);    w.Write(evtpend_);
        w.Write(reset_);     w.Write(evterr_);    w.Write(intrmask_);   w.Write(psw_);
        w.Write(evterrdbg_); w.Write(config_);    w.Write(once_enb_);   w.Write(once_data_);
        w.Write(once_instr_);w.Write(once_stat_); w.Write(once_cmd_);   w.Write(illinstaddr_);
        w.Write(chn0addr_);  w.Write(xtrig_conf1_);w.Write(xtrig_conf2_);
        w.WriteBytes(chnpri_, sizeof(chnpri_));
        w.WriteBytes(rx_cursor_, sizeof(rx_cursor_));
        SaveExtra(w);
    }
    void RestoreState(StateReader& r) override {
        r.Read(mc0ptr_);    r.Read(intr_);      r.Read(stop_stat_);  r.Read(hstart_);
        r.Read(evtovr_);    r.Read(dspovr_);    r.Read(hostovr_);    r.Read(evtpend_);
        r.Read(reset_);     r.Read(evterr_);    r.Read(intrmask_);   r.Read(psw_);
        r.Read(evterrdbg_); r.Read(config_);    r.Read(once_enb_);   r.Read(once_data_);
        r.Read(once_instr_);r.Read(once_stat_); r.Read(once_cmd_);   r.Read(illinstaddr_);
        r.Read(chn0addr_);  r.Read(xtrig_conf1_);r.Read(xtrig_conf2_);
        r.ReadBytes(chnpri_, sizeof(chnpri_));
        r.ReadBytes(rx_cursor_, sizeof(rx_cursor_));
        RestoreExtra(r);
    }

    /* Re-assert the INTC line from restored intr_ & intrmask_ - the SDMA AP
       interrupt is a level the source re-drives after restore. */
    void PostRestore() override { RefreshIrq(); }

    void RegisterSdmaEvent(uint32_t event, FreescaleSdmaPeripheral* p,
                           bool is_tx) override {
        if (event < kMaxDmaEvents) dma_events_[event] = DmaEventBinding{p, is_tx, true};
    }

    /* Fill consecutive owned (Done) RX buffer descriptors of the channel(s) bound
       (CHNENBL) to `event` with the received bytes, write each BD's received count
       into its mode/count word, and hand it back to the host (Done cleared) so the
       guest's DMA reader sees the data; then raise the channel IRQ. */
    bool SdmaRxDeliver(uint32_t event, const uint8_t* data,
                       std::size_t n) override {
        if (event >= kMaxDmaEvents || !dma_events_[event].used
            || dma_events_[event].is_tx || mc0ptr_ == 0)
            return false;
        const uint32_t chans = Chnenbl(event);

        if (chans == 0) return false;
        auto& mem = emu_.Get<EmulatedMemory>();
        std::size_t off = 0;
        bool delivered = false;
        for (uint32_t nch = 0; nch < kChannelCount && off < n; ++nch) {
            if ((chans & (1u << nch)) == 0) continue;
            uint8_t* ccb = mem.TryTranslateWrite(mc0ptr_ + nch * kCcbStride + kCcbBaseBdOff);
            if (ccb == nullptr) continue;
            /* The SDMA fills BDs in ring order from currentBDptr, wrapping at the
               W flag (MCIMX51RM Fig 52-82); track that position per channel so each
               received frame lands in the BD the guest's reader advances to, not
               always base. */
            const uint32_t base = *reinterpret_cast<uint32_t*>(ccb);
            if (rx_cursor_[nch] == 0) rx_cursor_[nch] = base;
            uint32_t bd_pa = rx_cursor_[nch];

            for (uint32_t i = 0; i < kMaxBdWalk && off < n; ++i) {
                uint8_t* bd = mem.TryTranslateWrite(bd_pa);
                if (bd == nullptr) break;
                uint32_t* word = reinterpret_cast<uint32_t*>(bd);

                if ((*word & kBdDone) == 0) break;          /* host owns -> not armed */
                const bool wrap     = (*word & kBdWrap) != 0;
                const bool want_irq = (*word & kBdIntr) != 0;
                const uint32_t stride = BdStride(*word);
                const uint32_t cap = *word & 0xFFFFu;        /* BD buffer size */
                const uint32_t buf_pa = word[1];
                uint32_t took = 0;
                for (; took < cap && off < n; ++took, ++off) {
                    uint8_t* dst = mem.TryTranslateWrite(buf_pa + took);
                    if (dst == nullptr) break;
                    *dst = data[off];
                }
                *word = (*word & ~(0xFFFFu | kBdDone | kBdError)) | took;

                if (want_irq) intr_ |= (1u << nch);
                delivered = true;
                bd_pa = wrap ? base : bd_pa + stride;
            }
            rx_cursor_[nch] = bd_pa;
        }

        if (delivered) RefreshIrq();
        return delivered;
    }

protected:
    /* Per-SoC interrupt line. i.MX31 -> Imx31Avic::Assert/DeassertSource(34);
       i.MX51 -> Imx51Tzic::AssertIrq/DeAssertIrq(6). */
    virtual void AssertIrqLine()   = 0;
    virtual void DeassertIrqLine() = 0;

    /* Per-SoC divergent registers (EVT_MIRROR, CHNENBL, and the i.MX51-only
       SDMA_LOCK / EVT_MIRROR2 / OTB / profile registers). Return true if handled. */
    virtual bool ReadExtra(uint32_t /*off*/, uint32_t& /*out*/)  { return false; }
    virtual bool WriteExtra(uint32_t /*off*/, uint32_t /*value*/) { return false; }
    virtual void SaveExtra(StateWriter&)    {}
    virtual void RestoreExtra(StateReader&) {}
    /* Concrete clears its own divergent registers on a software reset. */
    virtual void ResetExtra() {}

    /* Per-SoC CHNENBL[event] = channel bitmask (the DMA-request -> channel map,
       MCIMX51RM Section 52.4.3.3). Default 0 = nothing bound; a concrete that
       models CHNENBL overrides it so serial-DMA transfers find their channel. */
    virtual uint32_t Chnenbl(uint32_t /*event*/) const { return 0; }

    /* BD slot stride: 8 bytes (mode+buffer) or 12 (plus the extended-buffer word),
       set by the BD format the channel's SDMA script uses (MCIMX51RM Sec 52.23.1.1). */
    virtual uint32_t BdStride(uint32_t word0) const {
        return (word0 & kBdExtd) ? 12u : 8u;
    }

private:
    void CompleteChannels(uint32_t channels) {
        if (mc0ptr_ == 0) {
            HaltUnsupportedAccess("HSTART before MC0PTR set", kBase + kOffHstart, channels);
            return;
        }
        auto& mem = emu_.Get<EmulatedMemory>();
        for (uint32_t n = 0; n < kChannelCount; ++n) {
            if ((channels & (1u << n)) == 0) continue;
            const int ev = EventForChannel(n);

            FreescaleSdmaPeripheral* tx = nullptr;
            if (ev >= 0 && dma_events_[ev].used) {
                if (!dma_events_[ev].is_tx) continue;   /* bound RX: completes only via SdmaRxDeliver */
                tx = dma_events_[ev].p;                 /* bound TX: drain mem->peripheral below */
            } else if (n != 0) {
                /* Unbound non-config channel: clearing its armed BD's Done falsely
                   reports "data available" to the guest's RX reader, spinning its IST.
                   Channel 0 is the config channel whose BD Done the guest polls for
                   setup success (cspddk DDKSdmaInitChain), so ch0 still completes. */
                continue;
            }
            uint8_t* ccb = mem.TryTranslateWrite(mc0ptr_ + n * kCcbStride + kCcbBaseBdOff);
            if (ccb == nullptr) continue;
            uint32_t bd_pa = *reinterpret_cast<uint32_t*>(ccb);
            for (uint32_t i = 0; i < kMaxBdWalk; ++i) {
                uint8_t* bd = mem.TryTranslateWrite(bd_pa);
                if (bd == nullptr) break;
                uint32_t* word = reinterpret_cast<uint32_t*>(bd);
                if ((*word & kBdDone) == 0) break;  /* not owned by SDMA */
                const uint32_t stride = BdStride(*word);
                const bool want_irq = (*word & kBdIntr) != 0;
                if (tx != nullptr) {
                    const uint32_t count = *word & 0xFFFFu;
                    const uint32_t buf_pa = word[1];
                    for (uint32_t k = 0; k < count; ++k) {
                        uint8_t* src = mem.TryTranslateWrite(buf_pa + k);
                        if (src == nullptr) break;
                        tx->SdmaTxByte(*src);
                    }
                }
                *word &= ~(kBdDone | kBdError);
                if (want_irq) intr_ |= (1u << n);
                bd_pa += stride;
            }
        }
        RefreshIrq();
    }

    /* The peripheral DMA-request event mapped to channel `chan`, or -1 if none
       (reverse of CHNENBL: each event's register holds a one-bit channel mask). */
    int EventForChannel(uint32_t chan) const {
        for (uint32_t e = 0; e < kMaxDmaEvents; ++e)
            if (Chnenbl(e) & (1u << chan)) return static_cast<int>(e);
        return -1;
    }

    void RefreshIrq() {
        /* Any set INTR/HI[i] bit drives the AP IRQ (MCIMX51RM Table 52-13); HI[i]
           is raised per-BD by the word0 I flag (Table 52-96). INTRMASK (§52.12.3.11)
           is the EVTERR DMA-request-error mask (unmodeled here), not a completion gate. */
        if (intr_ != 0) AssertIrqLine();
        else            DeassertIrqLine();
    }

    void ResetCore() {
        mc0ptr_ = 0; intr_ = 0; stop_stat_ = 0; hstart_ = 0; evtovr_ = 0;
        dspovr_ = kResetDspovr; hostovr_ = 0; evtpend_ = 0; reset_ = 0; evterr_ = 0;
        intrmask_ = 0; psw_ = 0; evterrdbg_ = 0; config_ = kResetConfig;
        once_enb_ = 0; once_data_ = 0; once_instr_ = 0; once_stat_ = kResetOnceStat;
        once_cmd_ = 0; illinstaddr_ = kResetIllinstaddr; chn0addr_ = kResetChn0addr;
        xtrig_conf1_ = 0; xtrig_conf2_ = 0;
        for (uint32_t i = 0; i < kChannelCount; ++i) { chnpri_[i] = 0; rx_cursor_[i] = 0; }
        ResetExtra();
    }

    uint32_t mc0ptr_      = 0;
    uint32_t intr_        = 0;
    uint32_t stop_stat_   = 0;
    uint32_t hstart_      = 0;
    uint32_t evtovr_      = 0;
    uint32_t dspovr_      = kResetDspovr;
    uint32_t hostovr_     = 0;
    uint32_t evtpend_     = 0;
    uint32_t reset_       = 0;
    uint32_t evterr_      = 0;
    uint32_t intrmask_    = 0;
    uint32_t psw_         = 0;
    uint32_t evterrdbg_   = 0;
    uint32_t config_      = kResetConfig;
    uint32_t once_enb_    = 0;
    uint32_t once_data_   = 0;
    uint32_t once_instr_  = 0;
    uint32_t once_stat_   = kResetOnceStat;
    uint32_t once_cmd_    = 0;
    uint32_t illinstaddr_ = kResetIllinstaddr;
    uint32_t chn0addr_    = kResetChn0addr;
    uint32_t xtrig_conf1_ = 0;
    uint32_t xtrig_conf2_ = 0;
    uint32_t chnpri_[kChannelCount] = {};
    uint32_t rx_cursor_[kChannelCount] = {};   /* per-channel RX BD ring fill position (0 = uninit -> base) */

    static constexpr uint32_t kMaxDmaEvents = 48u;   /* i.MX51 CHNENBL count */
    struct DmaEventBinding {
        FreescaleSdmaPeripheral* p     = nullptr;
        bool                     is_tx = false;
        bool                     used  = false;
    };
    DmaEventBinding dma_events_[kMaxDmaEvents] = {};
};

}  /* namespace cerf_freescale_sdma_detail */
