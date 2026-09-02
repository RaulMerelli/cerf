#include "sd_card.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t kCcsHighCapacity = 1u << 30;  /* OCR CCS: SDHC/SDXC */
constexpr uint32_t kOcrBusyDone = 1u << 31;      /* OCR power-up status     */
constexpr uint32_t kOcrVoltageWin = 0x00FF8000u; /* 2.7-3.6 V window      */
/* SD Physical Layer Simplified Spec 3.01 Card Status (p. 75): bit 31
   OUT_OF_RANGE, "the command's argument was out of the allowed range
   for this card", clear condition C = clear by read (p. 76). */
constexpr uint32_t kStatusOutOfRange = 1u << 31;
} /* namespace */

SdCard::SdCard(uint64_t size_bytes) : media_(size_bytes) {
    BuildCidCsd();
    BuildExtCsd();
}

void SdCard::ConfigureKtp400(const std::string& device_dir, const std::string& container_name, KtpMobileOpType op_type,
                             KtpMobilePanel panel, const std::array<uint8_t, 6>& mac) {
    media_.ConfigureKtp400(device_dir, container_name, op_type, panel, mac);
}

/* CID (R2): MID, OID, product name, revision, serial, date. Only the layout
   matters to the driver; identity fields are nominal. CSD v2.0 (SDHC) encodes
   capacity as (C_SIZE+1) * 512 KB, C_SIZE = blocks/1024 - 1. */
void SdCard::BuildCidCsd() {
    std::memset(cid_, 0, sizeof(cid_));
    cid_[0] = 0x03; /* MID = SanDisk-ish nominal */
    cid_[1] = 'C';
    cid_[2] = 'E'; /* OID */
    cid_[3] = 'C';
    cid_[4] = 'E';
    cid_[5] = 'R';
    cid_[6] = 'F';
    cid_[7] = '0';
    cid_[8] = 0x10; /* product revision 1.0 */
    cid_[9] = 0x00;
    cid_[10] = 0x00;
    cid_[11] = 0x00;
    cid_[12] = 0x01; /* serial */
    cid_[13] = 0x01;
    cid_[14] = 0x40; /* mfg date */

    const uint64_t blocks = media_.Size() / 512u;
    std::memset(csd_, 0, sizeof(csd_));
    auto put_bits = [&](uint32_t hi, uint32_t lo, uint32_t value) {
        for (uint32_t b = lo; b <= hi; ++b) {
            const uint32_t bit = (value >> (b - lo)) & 1u;
            const uint32_t byte = (127u - b) / 8u;
            const uint32_t sh = b & 7u;
            if (bit) csd_[byte] |= static_cast<uint8_t>(1u << sh);
        }
    };

    /* KTP400 initializes the card via MMC CMD1/CMD3 and its CE stack parses the
       legacy MMC CSD capacity fields.  A SDHC/v2 CSD made it see only C_SIZE
       (4095 sectors), so advertise 512-byte sectors through C_SIZE_MULT too:
       (4095+1) * 2^(4+2) = 262144 sectors = 128 MB. */
    const uint32_t read_bl_len = 9u; /* 512 bytes */
    const uint32_t c_size_mult = 4u;
    const uint32_t c_size = static_cast<uint32_t>(std::min<uint64_t>((blocks >> (c_size_mult + 2u)) - 1u, 0x0FFFu));
    put_bits(127, 126, 0u);    /* CSD_STRUCTURE */
    put_bits(125, 122, 4u);    /* SPEC_VERS */
    put_bits(119, 112, 0x0Eu); /* TAAC */
    put_bits(103, 96, 0x32u);  /* TRAN_SPEED = 25 MHz */
    put_bits(95, 84, 0x5B5u);  /* CCC */
    put_bits(83, 80, read_bl_len);
    put_bits(73, 62, c_size);
    put_bits(49, 47, c_size_mult);
    put_bits(25, 22, read_bl_len); /* WRITE_BL_LEN */
    put_bits(14, 14, 1u);          /* COPY */

    /* SCR: SCR_STRUCTURE=0, SD_SPEC=2 (v2.0), SD_BUS_WIDTHS = 1-bit|4-bit. */
    std::memset(scr_, 0, sizeof(scr_));
    scr_[0] = 0x02;
    scr_[1] = 0x05;
}

/* EXT_CSD is persistent card state, not a synthetic one-shot response.  The
   KTP400 CE USDHC stack reads it, logs PART_CONFIG, then uses MMC CMD6 SWITCH
   to change bus/partition/timing fields.  Returning a sparse, immutable block
   leaves the driver waiting on a busy-switch path after MMC_EXT_CSD_BOOTCONF. */
void SdCard::BuildExtCsd() {
    std::memset(ext_csd_, 0, sizeof(ext_csd_));
    const uint32_t sectors = static_cast<uint32_t>(media_.Size() / 512u);

    ext_csd_[15] = 0x01;  /* S_CMD_SET */
    ext_csd_[160] = 0x07; /* PARTITIONING_SUPPORT: boot/RPMB/user cfg */
    ext_csd_[162] = 0x00; /* RST_N_FUNCTION */
    ext_csd_[179] = 0x48; /* PARTITION_CONFIG: boot ACK + boot partition 1 enabled; user area selected */
    ext_csd_[181] = 0x00; /* ERASE_GROUP_DEF: legacy erase group */
    ext_csd_[183] = 0x00; /* BUS_WIDTH: 1-bit initially */
    ext_csd_[185] = 0x00; /* HS_TIMING: legacy initially */
    ext_csd_[192] = 0x08; /* EXT_CSD_REV: eMMC 5.1 */
    ext_csd_[194] = 0x02; /* CSD_STRUCTURE */
    ext_csd_[196] = 0x03; /* CARD_TYPE: 26 MHz + 52 MHz */
    ext_csd_[197] = 0x01; /* DRIVER_STRENGTH */
    ext_csd_[199] = 0x01; /* PARTITION_SWITCH_TIME */
    ext_csd_[212] = static_cast<uint8_t>(sectors & 0xFFu);
    ext_csd_[213] = static_cast<uint8_t>((sectors >> 8) & 0xFFu);
    ext_csd_[214] = static_cast<uint8_t>((sectors >> 16) & 0xFFu);
    ext_csd_[215] = static_cast<uint8_t>((sectors >> 24) & 0xFFu);
    ext_csd_[221] = 0x01; /* HC_WP_GRP_SIZE */
    ext_csd_[222] = 0x01; /* REL_WR_SEC_C */
    ext_csd_[223] = 0x01; /* ERASE_TIMEOUT_MULT */
    ext_csd_[224] = 0x01; /* HC_ERASE_GRP_SIZE */
    ext_csd_[225] = 0x01; /* ACC_SIZE */
    ext_csd_[226] = 0x20; /* BOOT_MULT: 4 MiB boot partitions (128 KiB units) */
    ext_csd_[228] = 0x07; /* BOOT_INFO: alt boot + DDR + high speed */
    ext_csd_[494] = 0x01; /* CMD_SET: standard command set */
    mmc_partition_access_ = ext_csd_[179] & 0x07u;
}

void SdCard::ApplyMmcSwitch(uint32_t arg) {
    const uint8_t access = static_cast<uint8_t>((arg >> 24) & 0x03u);
    const uint8_t index = static_cast<uint8_t>((arg >> 16) & 0xFFu);
    const uint8_t value = static_cast<uint8_t>((arg >> 8) & 0xFFu);

    switch (access) {
    case 0: /* switch command set: accepted, no remap */ break;
    case 1: /* set bits */ ext_csd_[index] = static_cast<uint8_t>(ext_csd_[index] | value); break;
    case 2: /* clear bits */ ext_csd_[index] = static_cast<uint8_t>(ext_csd_[index] & ~value); break;
    case 3: /* write byte */ ext_csd_[index] = value; break;
    default: break;
    }

    if (index == 179u) /* PARTITION_CONFIG */
        mmc_partition_access_ = ext_csd_[179] & 0x07u;
}

uint32_t SdCard::Status() const {
    uint32_t current_state = 0u;
    switch (state_) {
    case State::Idle: current_state = 0u; break;
    case State::Ready: current_state = 1u; break;
    case State::Ident: current_state = 2u; break;
    case State::Stby: current_state = 3u; break;
    case State::Tran: current_state = 4u; break;
    case State::Data: current_state = 5u; break;
    case State::Rcv: current_state = 6u; break;
    }

    /* R1 status: bit 8 READY_FOR_DATA, bits 12:9 CURRENT_STATE.
       CE's SD/MMC stack polls CMD13 after writes; a zero status means "not
       ready" forever even though our in-RAM write completed synchronously. */
    const bool ready_for_data = (state_ == State::Tran || state_ == State::Stby);
    const uint32_t status = card_status_ | (ready_for_data ? (1u << 8) : 0u) | (current_state << 9);
    card_status_ &= ~kStatusOutOfRange;
    return status;
}

SdCard::CommandResult SdCard::Command(uint8_t index, uint32_t arg, bool app_cmd) {
    CommandResult r;
    auto r1 = [&] {
        r.rsp = Rsp::R1;
        r.resp[0] = Status();
    };

    if (app_cmd) {
        switch (index) {
        case 41: { /* ACMD41 SD_APP_OP_COND -> R3 (OCR) */
            r.rsp = Rsp::R3;
            acmd41_done_ = true;    /* single-step power-up: ready at once */
            high_capacity_ = false; /* 8 MB virtual media: SDSC byte addressing */
            r.resp[0] = kOcrBusyDone | kOcrVoltageWin;
            if (state_ == State::Idle) state_ = State::Ready;
            return r;
        }
        case 6: /* ACMD6 SET_BUS_WIDTH -> R1 */ r1(); return r;
        case 42: /* ACMD42 SET_CLR_CARD_DETECT -> R1 */ r1(); return r;
        case 51: /* ACMD51 SEND_SCR -> R1 + 8-byte SCR read */
            r1();
            r.starts_read = true;
            xfer_scr_ = true;
            return r;
        default: break;
        }
    }

    switch (index) {
    case 0: /* CMD0 GO_IDLE_STATE */
        state_ = State::Idle;
        acmd41_done_ = false;
        mmc_mode_ = false;
        media_.SetMmcMode(false);
        mmc_predefined_block_count_ = 0;
        mmc_reliable_write_ = false;
        r.rsp = Rsp::None;
        return r;
    case 1: { /* MMC/eMMC SEND_OP_COND -> R3 (OCR) */
        r.rsp = Rsp::R3;
        mmc_mode_ = true;
        media_.SetMmcMode(true);
        if (!mmc_layout_ready_) {
            media_.InitializeMmcLayout();
            mmc_layout_ready_ = true;
        }
        /* MMC CMD1/OCR access mode is a host/card contract.  Bit 30 asks for
           sector-addressed access; if it is not set, the card must remain in
           byte-addressed mode.  WinCE's sdmemory.dll uses its own high-capacity
           flag to decide whether CMD17/CMD18/CMD24/CMD25 arguments are sectors
           or byte offsets.  Forcing sector mode here breaks the clean path when
           the guest intentionally keeps byte-addressed MMC mode: the guest sends
           0x200 for sector 1, while the emulator reads LBA 0x200. */
        high_capacity_ = (arg & kCcsHighCapacity) != 0u;
        r.resp[0] = kOcrBusyDone | kOcrVoltageWin | (high_capacity_ ? kCcsHighCapacity : 0u);
        if (state_ == State::Idle) state_ = State::Ready;
        return r;
    }
    case 2: /* CMD2 ALL_SEND_CID -> R2 */
        r.rsp = Rsp::R2;
        std::memcpy(r.resp, cid_, 16);
        state_ = State::Ident;
        return r;
    case 3: /* SD SEND_RELATIVE_ADDR / MMC SET_RCA */
        if (mmc_mode_) {
            rca_ = static_cast<uint16_t>((arg >> 16) & 0xFFFFu);
            if (rca_ == 0) rca_ = 0x0001;
            r1();
        } else {
            rca_ = 0x0001;
            r.rsp = Rsp::R6;
            r.resp[0] = (static_cast<uint32_t>(rca_) << 16) | 0x0500; /* RCA|status */
        }
        state_ = State::Stby;
        return r;
    /* CMD5 is two different commands: SDIO IO_SEND_OP_COND, which a host uses
       to probe for an SDIO function, and MMC SLEEP_AWAKE.  JEDEC JESD84-B51
       6.10.4 gives SLEEP_AWAKE the card's RCA in arg[31:16], and an RCA of
       zero is never assigned, so a zero-RCA CMD5 is the SDIO probe even after
       the card has answered CMD1.  Answering that probe as SLEEP_AWAKE makes a
       host that probes MMC before SDIO believe an SDIO function is present. */
    case 5:
        if (mmc_mode_ && rca_ != 0u && static_cast<uint16_t>((arg >> 16) & 0xFFFFu) == rca_) {
            const bool sleep = ((arg >> 15) & 1u) != 0u;
            if (sleep) state_ = State::Stby; /* no sleep state in this minimal model */
            r.rsp = Rsp::R1b;
            r.resp[0] = Status();
            return r;
        }
        r.rsp = Rsp::R1;
        r.resp[0] = Status();
        r.illegal = true; /* SDIO CMD5 negative probe: no SDIO function */
        return r;
    case 6: /* SD SWITCH_FUNC / MMC SWITCH */
        if (mmc_mode_) {
            ApplyMmcSwitch(arg);
            r.rsp = Rsp::R1b;
            r.resp[0] = Status();
            return r;
        }
        r1();
        r.starts_read = true;
        xfer_switch_status_ = true;
        return r;
    case 7: /* CMD7 SELECT/DESELECT_CARD -> R1b */
        r.rsp = Rsp::R1b;
        state_ = (((arg >> 16) & 0xFFFFu) == rca_) ? State::Tran : State::Stby;
        r.resp[0] = Status();
        return r;
    case 8: /* SD SEND_IF_COND / MMC SEND_EXT_CSD */
        if (mmc_mode_) {
            r1();
            r.starts_read = true;
            xfer_ext_csd_ = true;
            return r;
        }
        r.rsp = Rsp::R7;
        r.resp[0] = arg & 0x00000FFFu; /* echo voltage + check pattern */
        return r;
    case 9: /* CMD9 SEND_CSD -> R2 */
        r.rsp = Rsp::R2;
        std::memcpy(r.resp, csd_, 16);
        return r;
    case 10: /* CMD10 SEND_CID -> R2 */
        r.rsp = Rsp::R2;
        std::memcpy(r.resp, cid_, 16);
        return r;
    case 12: /* CMD12 STOP_TRANSMISSION -> R1b */
        r.rsp = Rsp::R1b;
        state_ = State::Tran;
        mmc_predefined_block_count_ = 0;
        r.resp[0] = Status();
        return r;
    case 13: /* CMD13 SEND_STATUS -> R1 */
        if (mmc_mode_) media_.Commit();
        r1();
        return r;
    case 15: /* CMD15 GO_INACTIVE_STATE */
        state_ = State::Idle;
        r.rsp = Rsp::None;
        return r;
    case 16: /* CMD16 SET_BLOCKLEN -> R1 */
        blk_len_ = arg ? arg : 512u;
        r1();
        return r;
    case 17: /* CMD17 READ_SINGLE_BLOCK -> R1 + read */
    case 18: /* CMD18 READ_MULTIPLE_BLOCK -> R1 + read */
        r1();
        r.starts_read = true;
        xfer_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        if (mmc_mode_ && index != 18u) mmc_predefined_block_count_ = 0;
        state_ = State::Data;
        return r;
    case 23: /* CMD23 MMC SET_BLOCK_COUNT */
        /* Windows CE's eMMC stack uses CMD23 before multi-block reads/writes.
           Treating it as an illegal command gives uSDHC ERRI+CTOE (0x00018000)
           and the storage request completes as STATUS_DEVICE_NOT_READY. */
        mmc_predefined_block_count_ = arg & 0x0000FFFFu;
        mmc_reliable_write_ = (arg & 0x80000000u) != 0u;
        r1();
        return r;
    case 24: /* CMD24 WRITE_BLOCK -> R1 + write */
    case 25: /* CMD25 WRITE_MULTIPLE_BLOCK -> R1 + write */
        r1();
        r.starts_write = true;
        xfer_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        if (mmc_mode_ && index != 25u) mmc_predefined_block_count_ = 0;
        state_ = State::Rcv;
        return r;
    case 35: /* CMD35 ERASE_GROUP_START (MMC) */
        erase_start_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        erase_start_valid_ = true;
        r1();
        return r;
    case 36: /* CMD36 ERASE_GROUP_END (MMC) */
        erase_end_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        erase_end_valid_ = true;
        r1();
        return r;
    case 38: { /* CMD38 ERASE */
        if (erase_start_valid_ && erase_end_valid_) media_.Erase(erase_start_addr_, erase_end_addr_);
        erase_start_valid_ = false;
        erase_end_valid_ = false;
        r.rsp = Rsp::R1b;
        r.resp[0] = Status();
        state_ = State::Tran;
        return r;
    }
    case 55: /* CMD55 APP_CMD -> R1 */ r1(); return r;
    default:
        r.rsp = Rsp::R1;
        r.resp[0] = Status();
        r.illegal = true; /* honest: unsupported command */
        return r;
    }
}

void SdCard::ReadBlock(uint8_t* dst512) {
    if (xfer_ext_csd_) { /* MMC CMD8: 512-byte EXT_CSD register */
        std::memcpy(dst512, ext_csd_, sizeof(ext_csd_));
        xfer_ext_csd_ = false;
        return;
    }
    if (xfer_scr_) { /* ACMD51: 8-byte SCR, host reads 8 bytes */
        std::memset(dst512, 0, 512u);
        std::memcpy(dst512, scr_, sizeof(scr_));
        xfer_scr_ = false;
        return;
    }
    if (xfer_switch_status_) { /* CMD6: 512-bit switch-function status */
        std::memset(dst512, 0, 512u);
        /* Function group 1 (access mode): support default speed and high speed.
           Bytes are big-endian bit fields per SD Physical Layer §4.3.10. The
           KTP400 driver only needs a coherent, non-timeout status block. */
        dst512[0] = 0x00;
        dst512[1] = 0x00;
        dst512[2] = 0x00;
        dst512[3] = 0x03;
        dst512[13] = 0x80; /* data structure version 1.0 */
        dst512[16] = 0x00; /* current function: default speed */
        xfer_switch_status_ = false;
        return;
    }
    if (!media_.Read(xfer_addr_, dst512)) card_status_ |= kStatusOutOfRange;
    xfer_addr_ += 512u;
    if (mmc_predefined_block_count_ != 0) --mmc_predefined_block_count_;
    state_ = State::Tran;
}

void SdCard::WriteBlock(const uint8_t* src512) {
    if (!media_.Write(xfer_addr_, src512)) card_status_ |= kStatusOutOfRange;
    xfer_addr_ += 512u;
    if (mmc_predefined_block_count_ != 0) --mmc_predefined_block_count_;
    state_ = State::Tran;
}

void SdCard::CommitWrites() {
    media_.Commit();
}
