#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sd_card_media.h"

/* Generic SD/SDHC card model (SD Physical Layer Simplified Specification): the
   card-protocol layer an SDHCI/uSDHC host controller drives. Implements the
   boot-essential command subset (CMD0/2/3/6/7/8/9/13/16/17/18,
   CMD55+ACMD41/51/6, CMD24/25). Data moves through the host's FIFO; this owns
   card state + blocks. */

class SdCard {
public:
    /* Response kind the host latches into its RSP registers. */
    enum class Rsp : uint8_t { None, R1, R1b, R2, R3, R6, R7 };

    struct CommandResult {
        Rsp      rsp        = Rsp::None;
        uint32_t resp[4]    = {0, 0, 0, 0};  /* resp[0] = RSP0 (bits 39..8) */
        bool     illegal    = false;          /* set CMDIDX/illegal on host  */
        bool     starts_read  = false;        /* host should pump read FIFO  */
        bool     starts_write = false;        /* host should accept write FIFO */
    };

    /* size_bytes is rounded up to the 512-byte block size; backing is
       zero-initialised RAM unless LoadBackingFile succeeds. */
    explicit SdCard(uint64_t size_bytes);
    ~SdCard() = default;

    /* Process one host command. app_cmd reflects whether CMD55 immediately
       preceded this (so index N is interpreted as ACMDN). */
    CommandResult Command(uint8_t index, uint32_t arg, bool app_cmd);

    /* Block data path. The host calls these once a read/write command armed a
       transfer; offset advances internally per the active multi-block xfer. */
    void     ReadBlock(uint8_t* dst512);
    void     WriteBlock(const uint8_t* src512);
    void     CommitWrites();
    void     SetKtp400HardwareMac(const std::array<uint8_t, 6>& mac);

    bool present() const { return present_; }

private:
    enum class State : uint8_t { Idle, Ready, Ident, Stby, Tran, Data, Rcv };

    void     BuildCidCsd();
    void     BuildExtCsd();
    void     ApplyMmcSwitch(uint32_t arg);
    uint32_t Status() const;

    bool                 present_ = true;
    State                state_   = State::Idle;
    uint16_t             rca_     = 0;
    uint32_t             card_status_ = 0;
    uint32_t             blk_len_ = 512;
    bool                 high_capacity_ = false;
    uint64_t             xfer_addr_ = 0;    /* byte offset of next block       */
    bool                 acmd41_done_ = false;
    uint8_t              cid_[16] = {0};
    uint8_t              csd_[16] = {0};
    uint8_t              scr_[8]  = {0};
    uint8_t              ext_csd_[512] = {0};
    bool                 xfer_scr_ = false; /* next read returns SCR, not data */
    bool                 xfer_switch_status_ = false; /* next read returns CMD6 status */
    bool                 mmc_mode_ = false;  /* selected by CMD1 (eMMC/MMC init) */
    bool                 mmc_layout_ready_ = false;
    bool                 xfer_ext_csd_ = false; /* next read returns MMC EXT_CSD */
    uint8_t              mmc_partition_access_ = 0; /* EXT_CSD[179] PARTITION_CONFIG[2:0] */
    uint32_t             mmc_predefined_block_count_ = 0; /* MMC CMD23 SET_BLOCK_COUNT */
    bool                 mmc_reliable_write_ = false;     /* CMD23 arg[31] reliable write hint */
    uint64_t             erase_start_addr_ = 0;
    uint64_t             erase_end_addr_   = 0;
    bool                 erase_start_valid_ = false;
    bool                 erase_end_valid_   = false;
    SdCardMedia          media_;
};
