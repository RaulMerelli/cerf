#pragma once

#define NOMINMAX

#include "imx6_gic.h"
#include "imx6_usdhc_regs.h"
#include "imx6_usdhc_transfer.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include "imx6_id.h"
namespace cerf_imx6_usdhc_detail {

template <uint32_t kBase, int kSpi, bool kHasCard = true, uint32_t kSlot = 0u>
class Imx6UsdhcPort : public Imx6UsdhcTransfer {
public:
    using Imx6UsdhcTransfer::Imx6UsdhcTransfer;

protected:
    int Spi() const override { return kSpi; }
    bool HasCard() const override { return kHasCard; }
    uint32_t SlotIndex() const override { return kSlot; }

public:

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    uint32_t MmioBase() const override { return kBase; }
private:
    /* Linux imx6qdl.dtsi: reg = <base 0x4000> for each uSDHC instance. */
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t a) override {
        /* IMX6DQRM Rev.2 §67.8.9: DATA_BUFF is the 32-bit data port, and a read pops a whole word
           from the buffer; a narrower read would drop the rest of it. */
        if (((a & ~3u) - kBase) == kDATA_BUFF)
            HaltUnsupportedAccess("imx6-usdhc byte read of the data port", a, 0);
        return static_cast<uint8_t>(HandleRead((a & ~3u) - kBase) >> ((a & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t a) override {
        if (((a & ~3u) - kBase) == kDATA_BUFF)
            HaltUnsupportedAccess("imx6-usdhc half read of the data port", a, 0);
        return static_cast<uint16_t>(HandleRead((a & ~3u) - kBase) >> ((a & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t a) override { return HandleRead(a - kBase); }
    void WriteByte(uint32_t a, uint8_t v) override {
        const uint32_t off = (a & ~3u) - kBase;
        if (off == kDATA_BUFF) {
            WriteDataBytes(&v, 1u);
            return;
        }
        const uint32_t sh = (a & 3u) * 8u;
        if (off == kCMD_XFR_TYP) {
            cmd_xfr_typ_ = (cmd_xfr_typ_ & ~(0xFFu << sh)) | (static_cast<uint32_t>(v) << sh);
            if ((a & 3u) == 3u) ExecuteCommand(cmd_xfr_typ_);
            return;
        }
        HandleWrite(off, (HandleRead(off) & ~(0xFFu << sh)) | (static_cast<uint32_t>(v) << sh));
    }
    void WriteHalf(uint32_t a, uint16_t v) override {
        const uint32_t off = (a & ~3u) - kBase;
        if (off == kDATA_BUFF) {
            uint8_t tmp[2];
            std::memcpy(tmp, &v, sizeof(tmp));
            WriteDataBytes(tmp, 2u);
            return;
        }
        const uint32_t sh = (a & 2u) * 8u;
        if (off == kCMD_XFR_TYP) {
            cmd_xfr_typ_ = (cmd_xfr_typ_ & ~(0xFFFFu << sh)) | (static_cast<uint32_t>(v) << sh);
            /* Linux sdhci-esdhc-imx.c programs MIX_CTRL separately from CMD_XFR_TYP. */
            if ((a & 2u) != 0u) ExecuteCommand(cmd_xfr_typ_);
            return;
        }
        HandleWrite(off, (HandleRead(off) & ~(0xFFFFu << sh)) | (static_cast<uint32_t>(v) << sh));
    }
    void WriteWord(uint32_t a, uint32_t v) override { HandleWrite(a - kBase, v); }

    void SaveState(StateWriter& w) override {
        w.Write("cmdarg", cmdarg_);
        w.Write("cmd_xfr_typ", cmd_xfr_typ_);
        w.Write("mix_ctrl", mix_ctrl_);
        w.Write("irqstat", irqstat_);
        w.Write("irqstaten", irqstaten_);
        w.Write("irqsigen", irqsigen_);
        w.Write("sys_ctrl", sys_ctrl_);
        w.Write("prot_ctrl", prot_ctrl_);
        w.Write("blk_att", blk_att_);
        w.Write("wtmk_lvl", wtmk_lvl_);
        w.Write("vend_spec", vend_spec_);
        w.Write("ds_addr", ds_addr_);
        w.Write("adma_sys_addr", adma_sys_addr_);
        w.Write("autocmd12_err_status", autocmd12_err_status_);
        w.WriteBytes("rsp", rsp_, sizeof(rsp_));
        w.Write("buf_pos", buf_pos_);
        w.Write("blocks_rem", blocks_rem_);
        const uint32_t flags = (buf_reading_ ? 1u : 0u) | (buf_writing_ ? 2u : 0u) |
                               (open_ended_read_ ? 4u : 0u) | (open_ended_write_ ? 8u : 0u);
        w.Write("flags", flags);
        w.WriteBytes("buf", buf_, sizeof(buf_));

    }

    void RestoreState(StateReader& r) override {
        r.Read("cmdarg", cmdarg_);
        r.Read("cmd_xfr_typ", cmd_xfr_typ_);
        r.Read("mix_ctrl", mix_ctrl_);
        r.Read("irqstat", irqstat_);
        r.Read("irqstaten", irqstaten_);
        r.Read("irqsigen", irqsigen_);
        r.Read("sys_ctrl", sys_ctrl_);
        r.Read("prot_ctrl", prot_ctrl_);
        r.Read("blk_att", blk_att_);
        r.Read("wtmk_lvl", wtmk_lvl_);
        r.Read("vend_spec", vend_spec_);
        r.Read("ds_addr", ds_addr_);
        r.Read("adma_sys_addr", adma_sys_addr_);
        r.Read("autocmd12_err_status", autocmd12_err_status_);
        r.ReadBytes("rsp", rsp_, sizeof(rsp_));
        r.Read("buf_pos", buf_pos_);
        r.Read("blocks_rem", blocks_rem_);
        uint32_t flags = 0u;
        r.Read("flags", flags);
        buf_reading_ = (flags & 1u) != 0u;
        buf_writing_ = (flags & 2u) != 0u;
        open_ended_read_ = (flags & 4u) != 0u;
        open_ended_write_ = (flags & 8u) != 0u;
        r.ReadBytes("buf", buf_, sizeof(buf_));

    }

    void PostRestore() override { UpdateIrqLine(); }

private:
    uint32_t HandleRead(uint32_t off) {
        switch (off) {
        case kDS_ADDR: return ds_addr_;
        case kBLK_ATT: return blk_att_;
        case kCMD_ARG: return cmdarg_;
        case kCMD_XFR_TYP: return cmd_xfr_typ_;
        case kCMD_RSP0: return rsp_[0];
        case kCMD_RSP1: return rsp_[1];
        case kCMD_RSP2: return rsp_[2];
        case kCMD_RSP3: return rsp_[3];
        case kDATA_BUFF: return ReadData();
        case kPRES_STATE: {
            const uint32_t ps = PresentState();
            return ps;
        }

        case kSYS_CTRL: return sys_ctrl_;
        case kIRQSTAT: return irqstat_;
        case kIRQSTATEN: return irqstaten_;
        case kIRQSIGEN: return irqsigen_;
        case kAUTOCMD12: return autocmd12_err_status_;
        case kPROT_CTRL: return prot_ctrl_;
        case kWTMK_LVL: return wtmk_lvl_;
        case kVEND_SPEC: return vend_spec_;
        case kHOST_CAP: return kCapabilities;

        case kMIX_CTRL: return mix_ctrl_;
        case kADMA_SYS_ADDR: return adma_sys_addr_;

        case kHOST_VER: return kHostVersion;
        default: HaltUnsupportedAccess("imx6-usdhc read32 unmodelled register", kBase + off, 0);
        }
    }

    void HandleWrite(uint32_t off, uint32_t v) {
        switch (off) {
        case kDS_ADDR: ds_addr_ = v; return;
        /* IMX6DQRM Rev.2 §67.8.2: BLK_ATT "can be accessed only when no transaction is executing
           ... write operations will be ignored", and BLKSIZE ranges up to the buffer size, which
           for this model is one 512-byte block. */
        case kBLK_ATT:
            if (buf_reading_ || buf_writing_) return;
            if ((v & 0x1FFFu) == 0u || (v & 0x1FFFu) > sizeof(buf_))
                HaltUnsupportedAccess("imx6-usdhc BLK_ATT block size beyond the modelled buffer", kBase + off, v);
            blk_att_ = v;
            return;
        case kCMD_ARG: cmdarg_ = v; return;
        case kCMD_XFR_TYP: {
            cmd_xfr_typ_ = v;
            ExecuteCommand(cmd_xfr_typ_);
            return;
        }
        case kDATA_BUFF: WriteData(v); return;

        case kSYS_CTRL:
            /* IMX6DQRM Rev.2 §67.8 defines SYS_CTRL reset/INITA bits and
               PRES_STATE internal-clock stability. */
            sys_ctrl_ = v & ~(0x07000000u | kINITA);
            if (sys_ctrl_ & kCLK_INT_EN)
                sys_ctrl_ |= kCLK_INT_STBL;
            else
                sys_ctrl_ &= ~kCLK_INT_STBL;
            return;
        case kIRQSTAT:
            irqstat_ &= ~v;
            if ((v & kERRI) != 0u) irqstat_ &= ~kErrorSpecificMask;
            RefreshErrorSummary();
            /* QEMU SDHCI model (sdhci_update_irq). */
            if ((v & kBRR) != 0u && buf_reading_ &&
                (open_ended_read_ || buf_pos_ < std::max(4u, blk_att_ & 0x1FFFu) || blocks_rem_ > 0u))
                SetIrqStatus(kBRR);
            if ((v & kBWR) != 0u && buf_writing_ && (open_ended_write_ || buf_pos_ < std::max(4u, blk_att_ & 0x1FFFu)))
                SetIrqStatus(kBWR);
            UpdateIrqLine();
            return;
        case kIRQSTATEN:
            irqstaten_ = v;
            return;
        case kIRQSIGEN:
            irqsigen_ = v;
            UpdateIrqLine();
            return;
        case kPROT_CTRL:
            if ((v & kProtEndianMask) != kProtEndianLittle)
                HaltUnsupportedAccess("imx6-usdhc PROT_CTRL endian mode is not modelled", kBase + off, v);
            if ((v & kProtDmaSelMask) == kProtDmaSelAdma1 || (v & kProtDmaSelMask) == kProtDmaSelMask)
                HaltUnsupportedAccess("imx6-usdhc PROT_CTRL DMASEL is not modelled", kBase + off, v);
            if ((v & kProtUnmodelledMask) != 0u)
                HaltUnsupportedAccess("imx6-usdhc PROT_CTRL SDIO/card-detect control is not modelled", kBase + off, v);
            prot_ctrl_ = v;
            return;
        case kWTMK_LVL: wtmk_lvl_ = v; return;
        case kVEND_SPEC:
            if ((v & kVendVselect) != 0u)
                HaltUnsupportedAccess("imx6-usdhc VEND_SPEC 1.8 V pad select is not modelled", kBase + off, v);
            vend_spec_ = v;
            return;
        case kMIX_CTRL: mix_ctrl_ = v; return;
        case kADMA_SYS_ADDR: adma_sys_addr_ = v; return;

        default: HaltUnsupportedAccess("imx6-usdhc write32 unmodelled register", kBase + off, v);
        }
    }

};

}

using cerf_imx6_usdhc_detail::Imx6UsdhcPort;
