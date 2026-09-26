#pragma once

#include "imx6_gic.h"

#define NOMINMAX

#include "imx6_usdhc_adma.h"
#include "imx6_usdhc_regs.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/sd_card/sd_card.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

namespace cerf_imx6_usdhc_detail {

class Imx6UsdhcTransfer : public Peripheral {
public:
    using Peripheral::Peripheral;

protected:
    virtual int Spi() const = 0;
    virtual bool HasCard() const = 0;

    /* IMX6DQRM Rev.2 §67.8: with MIX_CTRL[AC12EN] the uSDHC issues CMD12 itself at the end of a
       multiple-block transfer and stores that R1b card status in CMDRSP3; AUTOCMD12_ERR_STATUS
       AC12NE[0] reports that it could not issue it, and IRQSTAT AC12E[24] follows that bit. */
    void IssueAutoCmd12() {
        if ((mix_ctrl_ & (kMixAutoCmd12 | kMixMultiBlk)) != (kMixAutoCmd12 | kMixMultiBlk)) return;
        if (HasCard()) {
            const SdCard::CommandResult res = Card().Command(12u, 0u, false);
            autocmd12_err_status_ &= ~kAc12NotExecuted;
            if (res.illegal) {
                autocmd12_err_status_ |= kAc12NotExecuted;
                SetIrqStatus(kAC12E);
                return;
            }
            rsp_[3] = res.resp[0];
        }
    }

    void ExecuteCommand(uint32_t cmd_xfr_typ) {
        const uint8_t idx = static_cast<uint8_t>((cmd_xfr_typ >> 24) & 0x3Fu);
        const bool dpsel = ((cmd_xfr_typ >> 21) & 1u) != 0u;
        const bool is_acmd = next_is_acmd_;
        next_is_acmd_ = (idx == 55u);

        SdCard::CommandResult res{};
        if (HasCard())
            res = Card().Command(idx, cmdarg_, is_acmd);
        else
            res.illegal = true;

        /* QEMU hw/sd/sdhci.c sdhci_send_command builds the R2 registers from the same bytes. */
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
            SetIrqStatus(kTC);
        }

        if (response_timeout) {
            SetIrqStatus(kCTOE);
        } else if (!pure_busy_response) {
            SetIrqStatus(kCC);
        }
        if (dpsel && dma_mode) {
            if (res.starts_read)
                AdmaDmaRead();
            else if (res.starts_write) {
                AdmaDmaWrite();
                Card().CommitWrites();
            }
            IssueAutoCmd12();
            SetIrqStatus(kTC | kDINT);
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
            SetIrqStatus(kBRR);
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
            SetIrqStatus(kBWR);
        } else if (dpsel && !res.illegal) {
            SetIrqStatus(kTC);
        } else if (pure_busy_response && !res.illegal && !response_timeout) {
            SetIrqStatus(kTC);
        }

        UpdateIrqLine();
    }

    uint32_t ReadData() {
        if (!buf_reading_ || buf_pos_ >= sizeof(buf_))
            HaltUnsupportedAccess("imx6-usdhc data port read outside a transfer", MmioBase() + kDATA_BUFF, 0);
        uint32_t val = 0u;
        std::memcpy(&val, buf_ + buf_pos_, 4u);
        buf_pos_ += 4u;
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        if (buf_pos_ >= blksize) {
            irqstat_ &= ~kBRR;
            if (open_ended_read_) {
                Card().ReadBlock(buf_);
                buf_pos_ = 0u;
                SetIrqStatus(kBRR);
            } else if (blocks_rem_ > 0u) {
                --blocks_rem_;
                Card().ReadBlock(buf_);
                buf_pos_ = 0u;
                SetIrqStatus(kBRR);
            } else {
                buf_reading_ = false;
                IssueAutoCmd12();
                SetIrqStatus(kTC);
            }
            UpdateIrqLine();
        }
        return val;
    }

    void WriteData(uint32_t v) {
        uint8_t tmp[4];
        std::memcpy(tmp, &v, sizeof(tmp));
        WriteDataBytes(tmp, 4u);
    }

    void WriteDataBytes(const uint8_t* src, uint32_t count) {
        if (!buf_writing_)
            HaltUnsupportedAccess("imx6-usdhc data port write outside a transfer", MmioBase() + kDATA_BUFF, 0);
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        const uint32_t limit = std::min<uint32_t>(sizeof(buf_), blksize);
        if (buf_pos_ >= limit) return;
        const uint32_t n = std::min<uint32_t>(count, limit - buf_pos_);
        std::memcpy(buf_ + buf_pos_, src, n);
        buf_pos_ += n;
        if (buf_pos_ >= blksize) {
            Card().WriteBlock(buf_);
            irqstat_ &= ~kBWR;
            if (open_ended_write_) {
                buf_pos_ = 0u;
                SetIrqStatus(kBWR);
            } else if (blocks_rem_ > 0u) {
                --blocks_rem_;
                buf_pos_ = 0u;
                SetIrqStatus(kBWR);
            } else {
                buf_writing_ = false;
                Card().CommitWrites();
                IssueAutoCmd12();
                SetIrqStatus(kTC);
            }
            UpdateIrqLine();
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
        /* IMX6DQRM Rev.2 §67.8.10: CINST[16]=0 and CDPL[18]=0 when no card present. */
        uint32_t p = kPS_CLK_STBL | kPS_DAT_IDLE | kPS_CMD_LVL;
        if (HasCard()) p |= kPS_CARD_PRES | kPS_CARD_DET | kPS_WP_LVL;
        const uint32_t blksize = std::max(4u, blk_att_ & 0x1FFFu);
        /* IMX6DQRM Rev.2 §67.8.10: BREN is high while "valid data greater than the watermark level
           exist in the buffer" and BWEN while that much space is free; §67.8.18 puts RD_WML in
           WTMK_LVL[7:0] and WR_WML in [23:16]. */
        const uint32_t words_left = (blksize - std::min(buf_pos_, blksize)) / 4u;
        if (buf_reading_ && (open_ended_read_ || buf_pos_ < blksize))
            p |= kPS_CMD_INH | kPS_DAT_INH | kPS_DLA | kPS_DRD |
                 (words_left >= (wtmk_lvl_ & 0xFFu) ? kPS_BUF_RDY : 0u);
        if (buf_writing_ && (open_ended_write_ || buf_pos_ < blksize))
            p |= kPS_CMD_INH | kPS_DAT_INH | kPS_DLA | kPS_DWR |
                 (words_left >= ((wtmk_lvl_ >> 16) & 0xFFu) ? kPS_BUF_SPC : 0u);
        return p;
    }

    /* IMX6DQRM Rev.2 §67.8.13 leaves ERRI a summary of the error bits, and INT_STATUS_EN has no
       enable of its own at bit 15. */
    void SetIrqStatus(uint32_t bits) {
        irqstat_ |= bits;
        RefreshErrorSummary();
    }

    void RefreshErrorSummary() {
        if ((irqstat_ & kErrorSpecificMask) != 0u) irqstat_ |= kERRI; else irqstat_ &= ~kERRI;
    }

    void UpdateIrqLine() {
        if (irqstat_ & irqsigen_)
            emu_.Get<::Imx6Gic>().AssertSpi(Spi());
        else
            emu_.Get<::Imx6Gic>().DeAssertSpi(Spi());
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
    uint32_t vend_spec_ = kVendSpecReset;
    uint32_t ds_addr_ = 0u;
    uint32_t adma_sys_addr_ = 0u;
    uint32_t autocmd12_err_status_ = 0u;
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

}
