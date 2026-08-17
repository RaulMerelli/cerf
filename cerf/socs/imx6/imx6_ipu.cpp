#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/lcd_scan_tick.h"
#include "../../host/host_window.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_ipu_cpmem.h"
#include "imx6_ipu_register_map.h"
#include "imx6_gic.h"

#include <cstdint>
#include <vector>

namespace {

using namespace imx6_ipu;

class Imx6Ipu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        regs_.assign(kSize / 4, 0u);
        ResetDisplayBlockDefaults();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        const uint32_t ipu_off = off & 0x000FFFFFu;
        EnsureModelledAccess("imx6-ipu read8 unmodelled register", addr, ipu_off, 0);
        return static_cast<uint8_t>(regs_[ipu_off >> 2] >> ((off & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off & 1u) HaltUnsupportedAccess("ReadHalf misaligned", addr, 0);
        const uint32_t ipu_off = off & 0x000FFFFFu;
        EnsureModelledAccess("imx6-ipu read16 unmodelled register", addr, ipu_off, 0);
        return static_cast<uint16_t>(regs_[ipu_off >> 2] >> ((off & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        const uint32_t ipu_off = off & 0x000FFFFFu;
        EnsureModelledAccess("imx6-ipu read32 unmodelled register", addr, ipu_off, 0);
        uint32_t value = regs_[ipu_off >> 2];
        if (ipu_off == kOffMemRst)
            value &= ~kRstMemStart;   /* internal-memory reset completes immediately */
        else if (ipu_off == kOffIdmacBusy1 || ipu_off == kOffIdmacBusy2)
            value = 0u;               /* DMA engine quiesces immediately in this model */
        else if (IsIdleStatusRegister(ipu_off))
            value = 0u;               /* display subblocks idle/complete when polled */
        return value;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - kBase, sh = (off & 3u) * 8u;
        const uint32_t ipu_off = off & 0x000FFFFFu;
        uint32_t& w = regs_[ipu_off >> 2];
        WriteMerged(off, (w & ~(0xFFu << sh)) | (static_cast<uint32_t>(value) << sh));
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - kBase;
        if (off & 1u) HaltUnsupportedAccess("WriteHalf misaligned", addr, value);
        const uint32_t sh = (off & 2u) * 8u;
        const uint32_t ipu_off = off & 0x000FFFFFu;
        uint32_t& w = regs_[ipu_off >> 2];
        WriteMerged(off, (w & ~(0xFFFFu << sh)) | (static_cast<uint32_t>(value) << sh));
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        WriteMerged(off, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(regs_.data(), regs_.size() * sizeof(uint32_t));
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_.data(), regs_.size() * sizeof(uint32_t));
    }
    void PostRestore() override {
        MaybeSignalDisplay();
    }

    void AdvanceScanTick() {
        auto* cp = emu_.TryGet<Imx6IpuCpmem>();
        if (!cp) return;
        const uint32_t ch = cp->ActiveDisplayChannel();
        if (ch >= 64u || !EnabledMaskForChannel(ch)) return;

        const uint32_t bit = 1u << (ch & 31u);
        const uint32_t ready0 = kOffChaBuf0Rdy0 + 4u * (ch / 32u);
        const uint32_t ready1 = kOffChaBuf1Rdy0 + 4u * (ch / 32u);
        const bool r0 = (regs_[ready0 >> 2] & bit) != 0u;
        const bool r1 = (regs_[ready1 >> 2] & bit) != 0u;
        if (!r0 && !r1) return;

        const uint32_t cur = kOffChaCurBuf0 + 4u * (ch / 32u);
        const bool use_buf1 = r1 && (!r0 || (regs_[cur >> 2] & bit) == 0u);
        if (use_buf1) {
            regs_[cur >> 2] |= bit;
            regs_[ready1 >> 2] &= ~bit;
        } else {
            regs_[cur >> 2] &= ~bit;
            regs_[ready0 >> 2] &= ~bit;
        }
        cp->SetCurrentBuffer(ch, use_buf1 ? 1u : 0u);

        RaiseDisplayFrameEvents(bit, ch >= 32u);
    }

private:
    void WriteMerged(uint32_t off, uint32_t value) {
        const uint32_t ipu_off = off & 0x000FFFFFu;
        EnsureModelledAccess("imx6-ipu write unmodelled register", kBase + off, ipu_off, value);
        if (ipu_off == kOffMemRst)
            value &= ~kRstMemStart;   /* internal-memory reset completes immediately */
        if (IsIpuIntStat(ipu_off)) {
            regs_[ipu_off >> 2] &= ~value; /* IPU_INT_STAT is W1C */
            UpdateInterruptLines();
            return;
        }
        if (IsIcOff(ipu_off)) {
            regs_[ipu_off >> 2] = value;
            return;
        }
        if (IsIpuIntCtrl(ipu_off)) {
            regs_[ipu_off >> 2] = value;
            UpdateInterruptLines();
            return;
        }
        if (IsIpuCurBuf(ipu_off)) {
            regs_[ipu_off >> 2] &= ~value; /* writing mask clears current-buffer bit */
            UpdateCpmemCurrentBuffer(ipu_off, value, 0u);
            return;
        }
        if (IsIpuBufReady(ipu_off)) {
            regs_[ipu_off >> 2] |= value;  /* guest marks buffers ready by writing mask */
            const bool high = (ipu_off == kOffChaBuf0Rdy0 + 4u) ||
                              (ipu_off == kOffChaBuf1Rdy0 + 4u);
            /* IPUv3 CHAx_BUFx_RDY is a software-owned ready bit that hardware
               clears when the IDMAC channel consumes the buffer.  It is not a
               direct current-buffer selector; CHAx_CUR_BUF changes on the DMA
               side at EOF.  Keep the CPMEM scanout buffer coupled to that EOF
               transition in AdvanceScanTick(), otherwise the guest can see a
               permanently busy buffer and stop queueing display work. */
            RaiseDisplayFrameEvents(value, high);
            MaybeSignalDisplay();
            return;
        }
        if (IsVdiOff(ipu_off)) {
            regs_[ipu_off >> 2] = value;
            return;
        }
        if (ipu_off == kOffIdmacChEn1) {
            const uint32_t display_mask = DisplayChannelMask();
            const bool was = (regs_[ipu_off >> 2] & display_mask) != 0u;
            regs_[ipu_off >> 2] = value;
            if (!was && (value & display_mask)) {
                RaiseDisplayFrameEvents(value & display_mask);
                MaybeSignalDisplay();
            }
            return;
        }
        if (ipu_off == kOffIdmacChEn2) {
            const uint32_t display_mask = DisplayChannelMaskHigh();
            const bool was = (regs_[ipu_off >> 2] & display_mask) != 0u;
            regs_[ipu_off >> 2] = value;
            if (!was && (value & display_mask)) {
                RaiseDisplayFrameEvents(value & display_mask, true);
                MaybeSignalDisplay();
            }
            return;
        }
        if (IsDmfcOff(ipu_off)) {
            regs_[ipu_off >> 2] = value;
            MaybeSignalDisplay();
            return;
        }
        if (IsDcOff(ipu_off)) {
            regs_[ipu_off >> 2] = value;
            MaybeSignalDisplay();
            return;
        }
        if (IsDiOff(ipu_off)) {
            regs_[ipu_off >> 2] = value;
            MaybeSignalDisplay();
            return;
        }
        regs_[ipu_off >> 2] = value;
        if (ipu_off == kOffConf || ipu_off == kOffIdmacChEn2 ||
            ipu_off == kOffFsDispFlow1 || ipu_off == kOffFsDispFlow2 ||
            IsDcOff(ipu_off) || IsDmfcOff(ipu_off) || IsDiOff(ipu_off) ||
            IsDpOff(ipu_off))
            MaybeSignalDisplay();
    }

    void EnsureModelledAccess(const char* what, uint32_t addr,
                              uint32_t ipu_off, uint32_t value) const {
        if (IsModelledRegister(ipu_off)) return;
        HaltUnsupportedAccess(what, addr, value);
    }

    void ResetDisplayBlockDefaults() {
        /* Linux drivers/gpu/ipu-v3/ipu-dmfc.c::ipu_dmfc_init reset programming.
           These are not guest-specific hacks: they are IPUv3 block reset/default
           state used before channel-specific tuning.  A fully zero DMFC makes
           the display pipe look programmed only after guest writes, whereas
           real hardware starts with sane DP/WR FIFO maps. */
        regs_[(kOffDmfcBase + kDmfcWrChan) >> 2] = 0x00000050u;
        regs_[(kOffDmfcBase + kDmfcDpChan) >> 2] = 0x00005654u;
        regs_[(kOffDmfcBase + kDmfcWrChanDef) >> 2] = 0x202020F6u;
        regs_[(kOffDmfcBase + kDmfcDpChanDef) >> 2] = 0x2020F6F6u;
        regs_[(kOffDmfcBase + kDmfcGeneral1) >> 2] = 0x00000003u;
    }




    void RaiseIpuIrq(uint32_t irq) {
        const uint32_t reg = irq / 32u;
        const uint32_t bit = irq & 31u;
        if (reg >= 15u) return;
        regs_[(kOffIntStat0 >> 2) + reg] |= 1u << bit;
        UpdateInterruptLines();
    }

    void RaiseDisplayFrameEvents(uint32_t channel_mask, bool high = false) {
        const uint32_t active = high ? 0u : (channel_mask & DisplayChannelMask());
        const uint32_t active_hi = high ? (channel_mask & DisplayChannelMaskHigh()) : 0u;
        if (!active && !active_hi) return;

        for (const uint32_t ch : kDisplayChannels) {
            const bool set = (ch < 32u) ? ((active & (1u << ch)) != 0u)
                                        : ((active_hi & (1u << (ch - 32u))) != 0u);
            if (set)
                RaiseIpuIrq(ch);       /* Linux: IPU_IRQ_EOF + channel */
        }
        RaiseIpuIrq(kIpuIrqVsyncPre0);
    }

    void UpdateInterruptLines() {
        static constexpr uint8_t kSyncRegs[] = {0, 1, 2, 3, 10, 11, 12, 13, 14};
        static constexpr uint8_t kErrRegs[]  = {4, 5, 8, 9};

        const auto any_enabled_pending = [this](const uint8_t* regs, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                const uint32_t n = regs[i];
                const uint32_t stat = regs_[(kOffIntStat0 >> 2) + n];
                const uint32_t ctrl = regs_[(kOffIntCtrl0 >> 2) + n];
                if (stat & ctrl) return true;
            }
            return false;
        };

        const bool sync = any_enabled_pending(kSyncRegs, sizeof(kSyncRegs));
        const bool err  = any_enabled_pending(kErrRegs, sizeof(kErrRegs));
        if (sync == sync_irq_asserted_ && err == err_irq_asserted_) return;

        if (auto* gic = emu_.TryGet<Imx6Gic>()) {
            if (sync != sync_irq_asserted_) {
                sync ? gic->AssertSpi(kIpuSyncSpi) : gic->DeAssertSpi(kIpuSyncSpi);
                sync_irq_asserted_ = sync;
            }
            if (err != err_irq_asserted_) {
                err ? gic->AssertSpi(kIpuErrSpi) : gic->DeAssertSpi(kIpuErrSpi);
                err_irq_asserted_ = err;
            }
        }
    }

    uint32_t EnabledMaskForChannel(uint32_t ch) const {
        const uint32_t reg = (ch < 32u) ? kOffIdmacChEn1 : kOffIdmacChEn2;
        return regs_[reg >> 2] & (1u << (ch & 31u));
    }

    uint32_t ReadyMaskForChannel(uint32_t ch) const {
        const uint32_t buf0 = kOffChaBuf0Rdy0 + 4u * (ch / 32u);
        const uint32_t buf1 = kOffChaBuf1Rdy0 + 4u * (ch / 32u);
        return (regs_[buf0 >> 2] | regs_[buf1 >> 2]) & (1u << (ch & 31u));
    }

    void UpdateCpmemCurrentBuffer(uint32_t ipu_off, uint32_t mask, uint32_t buffer) {
        auto* cp = emu_.TryGet<Imx6IpuCpmem>();
        if (!cp) return;
        const uint32_t bank = ((ipu_off == kOffChaCurBuf0 + 4u) ||
                               (ipu_off == kOffChaBuf0Rdy0 + 4u) ||
                               (ipu_off == kOffChaBuf1Rdy0 + 4u)) ? 32u : 0u;
        for (uint32_t bit = 0; bit < 32u; ++bit) {
            if (mask & (1u << bit))
                cp->SetCurrentBuffer(bank + bit, buffer);
        }
    }

    bool DisplayPipeConfigured() const {
        const uint32_t conf = regs_[kOffConf >> 2];
        const bool idmac_ok = (conf & kConfIdmacDis) == 0u;
        const bool core_pipe = (conf & (kConfDcEn | kConfDmfcEn)) == (kConfDcEn | kConfDmfcEn);
        const bool di_ok = (conf & (kConfDi0En | kConfDi1En)) != 0u;
        const bool dc_programmed =
            regs_[(kOffDcBase + 5u * kDcChStride + kDcWrChConf) >> 2] != 0u ||
            regs_[(kOffDcBase + 1u * kDcChStride + kDcWrChConf) >> 2] != 0u ||
            regs_[(kOffDcBase + kDcGen) >> 2] != 0u ||
            regs_[(kOffDcBase + kDcDispConf1_0) >> 2] != 0u ||
            regs_[(kOffDcBase + kDcDispConf2_0) >> 2] != 0u;
        const bool dmfc_programmed =
            regs_[(kOffDmfcBase + kDmfcDpChan) >> 2] != 0u ||
            regs_[(kOffDmfcBase + kDmfcWrChan) >> 2] != 0u ||
            regs_[(kOffDmfcBase + kDmfcGeneral1) >> 2] != 0u;
        return idmac_ok && core_pipe && di_ok && dc_programmed && dmfc_programmed;
    }

    void MaybeSignalDisplay() {
        auto* cp = emu_.TryGet<Imx6IpuCpmem>();
        if (!cp) return;
        for (const uint32_t ch : kDisplayChannels) {
            if (!EnabledMaskForChannel(ch) || !ReadyMaskForChannel(ch)) continue;
            const auto d = cp->DecodeChannel(ch);
            if (d.valid) {
                cp->SetActiveDisplayChannel(ch);
                emu_.Get<HostWindow>().OnLcdEnabled(d.fw, d.fh);
                return;
            }
        }
    }

    std::vector<uint32_t> regs_;
    bool sync_irq_asserted_ = false;
    bool err_irq_asserted_ = false;
};

class Imx6IpuScanTick : public LcdScanTick {
public:
    using LcdScanTick::LcdScanTick;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    void OnHostTick() override {
        emu_.Get<Imx6Ipu>().AdvanceScanTick();
    }
};

}  /* namespace */

REGISTER_SERVICE(Imx6Ipu);
REGISTER_SERVICE_AS(Imx6IpuScanTick, LcdScanTick);
