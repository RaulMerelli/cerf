#include "imx6_gic.h"

#define NOMINMAX

#include "imx6_usdhc_adma.h"
#include "imx6_usdhc_regs.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#pragma once

#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../peripherals/sd_card/sd_card.h"
#include "../../peripherals/sd_card/sd_card_configuration.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
namespace {

static constexpr uint64_t kCardBytes = 128ull * 1024u * 1024u;

template <uint32_t kBase, int kSpi, bool kHasCard = true> class Imx6UsdhcPort : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        if constexpr (kHasCard) {
            card_.emplace(kCardBytes);
            if (auto* config = emu_.TryGet<SdCardConfiguration>()) config->Configure(*card_);
        }
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    uint32_t MmioBase() const override { return kBase; }


private:
    /* Linux imx6qdl.dtsi: reg = <base 0x4000> for each uSDHC instance. */
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t a) override {
        if (((a & ~3u) - kBase) == kDATA_BUFF) return static_cast<uint8_t>(ReadData() >> ((a & 3u) * 8u));
        return static_cast<uint8_t>(HandleRead((a & ~3u) - kBase) >> ((a & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t a) override {
        if (((a & ~3u) - kBase) == kDATA_BUFF) return static_cast<uint16_t>(ReadData() >> ((a & 2u) * 8u));
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
            /* Linux's i.MX6 uSDHC driver writes MIX_CTRL for transfer mode and
               CMD_XFR_TYP[31:16] for the command.  Generic SDHCI drivers may
               write the low transfer-mode half first.  Do not launch a command
               until the command halfword at offset +0x0E is written. */
            if ((a & 2u) != 0u) ExecuteCommand(cmd_xfr_typ_);
            return;
        }
        HandleWrite(off, (HandleRead(off) & ~(0xFFFFu << sh)) | (static_cast<uint32_t>(v) << sh));
    }
    void WriteWord(uint32_t a, uint32_t v) override { HandleWrite(a - kBase, v); }

    void SaveState(StateWriter& w) override {
        w.Write(cmdarg_);
        w.Write(cmd_xfr_typ_);
        w.Write(mix_ctrl_);
        w.Write(irqstat_);
        w.Write(irqstaten_);
        w.Write(irqsigen_);
        w.Write(sys_ctrl_);
        w.Write(prot_ctrl_);
        w.Write(blk_att_);
        w.Write(wtmk_lvl_);
        w.Write(vend_spec_);
        w.Write(ds_addr_);
        w.Write(adma_sys_addr_);
        w.WriteBytes(rsp_, sizeof(rsp_));
        w.Write(buf_pos_);
        w.Write(blocks_rem_);
        const uint32_t flags = (buf_reading_ ? 1u : 0u) | (buf_writing_ ? 2u : 0u) | (next_is_acmd_ ? 4u : 0u) |
                               (open_ended_read_ ? 8u : 0u) | (open_ended_write_ ? 16u : 0u);
        w.Write(flags);
        w.WriteBytes(buf_, sizeof(buf_));
    }

    void RestoreState(StateReader& r) override {
        r.Read(cmdarg_);
        r.Read(cmd_xfr_typ_);
        r.Read(mix_ctrl_);
        r.Read(irqstat_);
        r.Read(irqstaten_);
        r.Read(irqsigen_);
        r.Read(sys_ctrl_);
        r.Read(prot_ctrl_);
        r.Read(blk_att_);
        r.Read(wtmk_lvl_);
        r.Read(vend_spec_);
        r.Read(ds_addr_);
        r.Read(adma_sys_addr_);
        r.ReadBytes(rsp_, sizeof(rsp_));
        r.Read(buf_pos_);
        r.Read(blocks_rem_);
        uint32_t flags = 0u;
        r.Read(flags);
        buf_reading_ = (flags & 1u) != 0u;
        buf_writing_ = (flags & 2u) != 0u;
        next_is_acmd_ = (flags & 4u) != 0u;
        open_ended_read_ = (flags & 8u) != 0u;
        open_ended_write_ = (flags & 16u) != 0u;
        r.ReadBytes(buf_, sizeof(buf_));
    }

    void PostRestore() override { AssertIfEnabled(); }

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
        case kPROT_CTRL: return prot_ctrl_;
        case kSYS_CTRL: return sys_ctrl_;
        case kIRQSTAT: return irqstat_;
        case kIRQSTATEN: return irqstaten_;
        case kIRQSIGEN: return irqsigen_;
        case kAUTOCMD12: return 0u;
        case kHOST_CAP: return kCapabilities;
        case kWTMK_LVL: return wtmk_lvl_;
        case kMIX_CTRL: return mix_ctrl_;
        case kADMA_SYS_ADDR: return adma_sys_addr_;
        case kVEND_SPEC: return vend_spec_;
        case kHOST_VER: return kHostVersion;
        default: HaltUnsupportedAccess("imx6-usdhc read32 unmodelled register", kBase + off, 0);
        }
    }

    void HandleWrite(uint32_t off, uint32_t v) {
        switch (off) {
        case kDS_ADDR: ds_addr_ = v; return;
        case kBLK_ATT: blk_att_ = v; return;
        case kCMD_ARG: cmdarg_ = v; return;
        case kCMD_XFR_TYP: {
            cmd_xfr_typ_ = v;
            ExecuteCommand(cmd_xfr_typ_);
            return;
        }
        case kDATA_BUFF: WriteData(v); return;
        case kPRES_STATE: return; /* read-only */
        case kPROT_CTRL: prot_ctrl_ = v; return;
        case kSYS_CTRL:
            /* SWRST_ALL[24]/CMD[25]/DATA[26] and INITA[27] self-clear in HW
               (SDHCI spec §2.2.15 for resets; kINITA for 80-clock init sequence).
               CLOCK_INT_STABLE[1] is asserted immediately when CLOCK_INT_EN[0]
               is written — the driver polls bit 1 before touching any SD command. */
            sys_ctrl_ = v & ~(0x07000000u | kINITA);
            if (sys_ctrl_ & kCLK_INT_EN)
                sys_ctrl_ |= kCLK_INT_STBL;
            else
                sys_ctrl_ &= ~kCLK_INT_STBL;
            return;
        case kIRQSTAT: /* W1C — clear bits that the driver acknowledged. */
            irqstat_ &= ~v;
            /* ERRI is the normal-status summary for the detailed error bits.
               The KTP400 WinCE driver commonly acknowledges CTOE itself; real
               eSDHC then drops the summary once no detailed error remains.
               Leaving ERRI sticky makes later successful CMD1/CMD2/CMD3 look
               as if they inherited the SDIO-probe timeout. */
            if ((v & kErrorSpecificMask) != 0u && (irqstat_ & kErrorSpecificMask) == 0u) irqstat_ &= ~kERRI;
            if ((v & kERRI) != 0u) irqstat_ &= ~kErrorSpecificMask;
            /* BRR is level-triggered: hardware holds it asserted as long as read-buffer
               data exceeds the watermark, even after a software W1C write.
               Reference: QEMU sdhci.c sdhci_update_irq(). */
            if ((v & kBRR) != 0u && buf_reading_ &&
                (open_ended_read_ || buf_pos_ < std::max(4u, blk_att_ & 0x1FFFu) || blocks_rem_ > 0u))
                irqstat_ |= kBRR;
            if ((v & kBWR) != 0u && buf_writing_ && (open_ended_write_ || buf_pos_ < std::max(4u, blk_att_ & 0x1FFFu)))
                irqstat_ |= kBWR;
            DeassertIfEmpty();
            return;
        case kIRQSTATEN: irqstaten_ = v; return;
        case kIRQSIGEN:
            irqsigen_ = v;
            AssertIfEnabled();
            return;
        case kAUTOCMD12: return; /* status register, ignore writes */
        case kHOST_CAP: return;  /* HW-init: ignore */
        case kWTMK_LVL: wtmk_lvl_ = v; return;
        case kMIX_CTRL: mix_ctrl_ = v; return;
        case kADMA_SYS_ADDR: adma_sys_addr_ = v; return;
        case kVEND_SPEC: vend_spec_ = v; return;
        default: HaltUnsupportedAccess("imx6-usdhc write32 unmodelled register", kBase + off, v);
        }
    }

    void ExecuteCommand(uint32_t cmd_xfr_typ) {
        const uint8_t idx = static_cast<uint8_t>((cmd_xfr_typ >> 24) & 0x3Fu);
        const bool dpsel = ((cmd_xfr_typ >> 21) & 1u) != 0u;
        const bool is_acmd = next_is_acmd_;
        next_is_acmd_ = (idx == 55u);

        SdCard::CommandResult res{};
        if constexpr (kHasCard)
            res = Card().Command(idx, cmdarg_, is_acmd);
        else
            res.illegal = true;

        /* R2 byte indices from QEMU sdhci.c L358-362; wrong indices garble CMD2/CMD9 CID/CSD. */
        if (res.rsp == SdCard::Rsp::R2) {
            const auto* b = reinterpret_cast<const uint8_t*>(res.resp);
            rsp_[0] = (static_cast<uint32_t>(b[11]) << 24) | (static_cast<uint32_t>(b[12]) << 16) |
                      (static_cast<uint32_t>(b[13]) << 8) | static_cast<uint32_t>(b[14]);
            rsp_[1] = (static_cast<uint32_t>(b[7]) << 24) | (static_cast<uint32_t>(b[8]) << 16) |
                      (static_cast<uint32_t>(b[9]) << 8) | static_cast<uint32_t>(b[10]);
            rsp_[2] = (static_cast<uint32_t>(b[3]) << 24) | (static_cast<uint32_t>(b[4]) << 16) |
                      (static_cast<uint32_t>(b[5]) << 8) | static_cast<uint32_t>(b[6]);
            rsp_[3] =
                (static_cast<uint32_t>(b[0]) << 16) | (static_cast<uint32_t>(b[1]) << 8) | static_cast<uint32_t>(b[2]);
        } else {
            rsp_[0] = res.resp[0];
            rsp_[1] = rsp_[2] = rsp_[3] = 0u;
        }
        const bool response_timeout = res.illegal && (((cmd_xfr_typ >> 16) & 3u) != 0u);
        const bool dma_mode = (mix_ctrl_ & kMixDmaEn) != 0u;
        const bool busy_response = (res.rsp == SdCard::Rsp::R1b);
        const bool pure_busy_response = busy_response && !dpsel && !res.starts_read && !res.starts_write;

        if (idx == 12u && !response_timeout) {
            buf_reading_ = false;
            buf_writing_ = false;
            open_ended_read_ = false;
            open_ended_write_ = false;
            irqstat_ &= ~(kBRR | kBWR);
            irqstat_ |= kTC;
        }

        if (response_timeout) {
            irqstat_ |= kERRI | kCTOE;
        } else if (!pure_busy_response) {
            irqstat_ |= kCC;
        }
        /* i.MX6 eSDHC MIX_CTRL maps 1:1 to the SDHCI Transfer Mode Register
           for the low transfer bits.  DMAEN selects ADMA2 vs PIO; BCEN/MSBSEL
           are already reflected in BLK_ATT and the command type. */
        if (dpsel && dma_mode) {
            if (res.starts_read)
                AdmaDmaRead();
            else if (res.starts_write) {
                AdmaDmaWrite();
                Card().CommitWrites();
            }
            irqstat_ |= kTC | kDINT;
        } else if (dpsel && res.starts_read && !is_acmd && idx == 6u) {
            Card().ReadBlock(buf_);
            irqstat_ |= kTC;
            buf_reading_ = false;
            buf_writing_ = false;
        } else if (dpsel && res.starts_read) {
            const uint32_t blkcnt = (blk_att_ >> 16) & 0xFFFFu;
            const bool multi = ((mix_ctrl_ & kMixMultiBlk) != 0u) || idx == 18u;
            const bool count_limited = !multi || ((mix_ctrl_ & kMixBlkCntEn) != 0u);
            const uint32_t total = count_limited ? ((blkcnt != 0u) ? blkcnt : 1u) : 1u;
            blocks_rem_ = count_limited ? (total - 1u) : 0u;
            open_ended_read_ = !count_limited;
            open_ended_write_ = false;
            Card().ReadBlock(buf_);
            buf_pos_ = 0u;
            buf_reading_ = true;
            buf_writing_ = false;
            irqstat_ |= kBRR;
        } else if (dpsel && res.starts_write) {
            const uint32_t blkcnt = (blk_att_ >> 16) & 0xFFFFu;
            const bool multi = ((mix_ctrl_ & kMixMultiBlk) != 0u) || idx == 25u;
            const bool count_limited = !multi || ((mix_ctrl_ & kMixBlkCntEn) != 0u);
            const uint32_t total = count_limited ? ((blkcnt != 0u) ? blkcnt : 1u) : 1u;
            blocks_rem_ = count_limited ? (total - 1u) : 0u;
            open_ended_read_ = false;
            open_ended_write_ = !count_limited;
            buf_pos_ = 0u;
            buf_reading_ = false;
            buf_writing_ = true;
            irqstat_ |= kBWR;
        } else if (dpsel && !res.illegal) {
            irqstat_ |= kTC;
        } else if (pure_busy_response && !res.illegal && !response_timeout) {
            irqstat_ |= kTC;
        }

        AssertIfEnabled();
    }

    uint32_t ReadData() {
        if (!buf_reading_ || buf_pos_ >= 512u) return 0u;
        uint32_t val = 0u;
        std::memcpy(&val, buf_ + buf_pos_, 4u);
        buf_pos_ += 4u;
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        if (buf_pos_ >= blksize) {
            irqstat_ &= ~kBRR;
            if (open_ended_read_) {
                Card().ReadBlock(buf_);
                buf_pos_ = 0u;
                irqstat_ |= kBRR;
            } else if (blocks_rem_ > 0u) {
                --blocks_rem_;
                Card().ReadBlock(buf_);
                buf_pos_ = 0u;
                irqstat_ |= kBRR;
            } else {
                buf_reading_ = false;
                irqstat_ |= kTC;
            }
            AssertIfEnabled();
        }
        return val;
    }

    void WriteData(uint32_t v) {
        uint8_t tmp[4];
        std::memcpy(tmp, &v, sizeof(tmp));
        WriteDataBytes(tmp, 4u);
    }

    void WriteDataBytes(const uint8_t* src, uint32_t count) {
        if (!buf_writing_ || buf_pos_ >= 512u) return;
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        const uint32_t n = std::min<uint32_t>(count, std::min<uint32_t>(512u, blksize) - buf_pos_);
        std::memcpy(buf_ + buf_pos_, src, n);
        buf_pos_ += n;
        if (buf_pos_ >= blksize) {
            Card().WriteBlock(buf_);
            irqstat_ &= ~kBWR;
            if (open_ended_write_) {
                buf_pos_ = 0u;
                irqstat_ |= kBWR;
            } else if (blocks_rem_ > 0u) {
                --blocks_rem_;
                buf_pos_ = 0u;
                irqstat_ |= kBWR;
            } else {
                buf_writing_ = false;
                Card().CommitWrites();
                irqstat_ |= kTC;
            }
            AssertIfEnabled();
        }
    }

    void AdmaDmaRead() {
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        const Imx6UsdhcAdma::Transfer transfer{adma_sys_addr_ ? adma_sys_addr_ : ds_addr_, blksize,
                                               (blk_att_ >> 16) & 0xFFFFu, (mix_ctrl_ & kMixBlkCntEn) != 0u};
        emu_.Get<Imx6UsdhcAdma>().Read(Card(), transfer, buf_);
    }

    void AdmaDmaWrite() {
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        const Imx6UsdhcAdma::Transfer transfer{adma_sys_addr_ ? adma_sys_addr_ : ds_addr_, blksize,
                                               (blk_att_ >> 16) & 0xFFFFu, (mix_ctrl_ & kMixBlkCntEn) != 0u};
        emu_.Get<Imx6UsdhcAdma>().Write(Card(), transfer, buf_);
    }

    uint32_t PresentState() const {
        /* IMX6SDLRM §67.8.10: CINST[16]=0 and CDPL[18]=0 when no card present.
           Bus lines remain HIGH (pulled up) regardless of card presence. */
        uint32_t p = kPS_CLK_STBL | kPS_DAT_IDLE | kPS_CMD_LVL;
        if (kHasCard) p |= kPS_CARD_PRES | kPS_CARD_DET | kPS_WP_LVL;
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        if (buf_reading_ && (open_ended_read_ || buf_pos_ < blksize))
            p |= kPS_CMD_INH | kPS_DAT_INH | kPS_DLA | kPS_BUF_RDY | kPS_DRD;
        if (buf_writing_ && (open_ended_write_ || buf_pos_ < blksize))
            p |= kPS_CMD_INH | kPS_DAT_INH | kPS_DLA | kPS_BUF_SPC | kPS_DWR;
        return p;
    }

    void AssertIfEnabled() {
        if (irqstat_ & irqsigen_) {
            emu_.Get<::Imx6Gic>().AssertSpi(kSpi);
        }
    }

    void DeassertIfEmpty() {
        if (!(irqstat_ & irqsigen_)) emu_.Get<::Imx6Gic>().DeAssertSpi(kSpi);
    }

    SdCard& Card() { return *card_; }

    std::optional<SdCard> card_;
    uint32_t cmdarg_ = 0u;
    uint32_t cmd_xfr_typ_ = 0u;
    uint32_t mix_ctrl_ = 0u;
    uint32_t irqstat_ = 0u;
    uint32_t irqstaten_ = 0u;
    uint32_t irqsigen_ = 0u;
    uint32_t sys_ctrl_ = 0u;
    uint32_t prot_ctrl_ = 0u;
    uint32_t blk_att_ = 0u;
    uint32_t wtmk_lvl_ = 0u;
    uint32_t vend_spec_ = 0u;
    uint32_t ds_addr_ = 0u;
    uint32_t adma_sys_addr_ = 0u;
    uint32_t rsp_[4] = {};

    uint8_t buf_[512] = {};
    uint32_t buf_pos_ = 0u;
    uint32_t blocks_rem_ = 0u;
    bool buf_reading_ = false;
    bool buf_writing_ = false;
    bool next_is_acmd_ = false;
    bool open_ended_read_ = false;
    bool open_ended_write_ = false;
};

} // namespace
