#include "sd_card.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t kCcsHighCapacity = 1u << 30;
constexpr uint32_t kOcrBusyDone = 1u << 31;
constexpr uint32_t kOcrVoltageWin = 0x00FF8000u;
constexpr uint32_t kStatusOutOfRange = 1u << 31;
}

SdCard::SdCard(uint64_t size_bytes) : media_(size_bytes) {
    BuildCidCsd();
    BuildExtCsd();
}

void SdCard::ConfigureMedia(std::unique_ptr<SdCardMediaBackend> backend) {
    media_.Configure(std::move(backend));
}

void SdCard::SaveState(StateWriter& w) const {
    w.Write(present_);
    w.Write(state_);
    w.Write(rca_);
    w.Write(card_status_);
    w.Write(blk_len_);
    w.Write(high_capacity_);
    w.Write(xfer_addr_);
    w.Write(acmd41_done_);
    w.WriteBytes(ext_csd_, sizeof(ext_csd_));
    w.Write(xfer_scr_);
    w.Write(xfer_switch_status_);
    w.Write(mmc_mode_);
    w.Write(mmc_layout_ready_);
    w.Write(xfer_ext_csd_);
    w.Write(mmc_partition_access_);
    w.Write(mmc_predefined_block_count_);
    w.Write(mmc_reliable_write_);
    w.Write(erase_start_addr_);
    w.Write(erase_end_addr_);
    w.Write(erase_start_valid_);
    w.Write(erase_end_valid_);
}

void SdCard::RestoreState(StateReader& r) {
    r.Read(present_);
    r.Read(state_);
    r.Read(rca_);
    r.Read(card_status_);
    r.Read(blk_len_);
    r.Read(high_capacity_);
    r.Read(xfer_addr_);
    r.Read(acmd41_done_);
    r.ReadBytes(ext_csd_, sizeof(ext_csd_));
    r.Read(xfer_scr_);
    r.Read(xfer_switch_status_);
    r.Read(mmc_mode_);
    r.Read(mmc_layout_ready_);
    r.Read(xfer_ext_csd_);
    r.Read(mmc_partition_access_);
    r.Read(mmc_predefined_block_count_);
    r.Read(mmc_reliable_write_);
    r.Read(erase_start_addr_);
    r.Read(erase_end_addr_);
    r.Read(erase_start_valid_);
    r.Read(erase_end_valid_);
    if (mmc_layout_ready_) media_.InitializeMmcLayout();
    media_.SetMmcMode(mmc_mode_);
}

void SdCard::BuildCidCsd() {
    std::memset(cid_, 0, sizeof(cid_));
    cid_[0] = 0x03;
    cid_[1] = 'C';
    cid_[2] = 'E';
    cid_[3] = 'C';
    cid_[4] = 'E';
    cid_[5] = 'R';
    cid_[6] = 'F';
    cid_[7] = '0';
    cid_[8] = 0x10;
    cid_[9] = 0x00;
    cid_[10] = 0x00;
    cid_[11] = 0x00;
    cid_[12] = 0x01;
    cid_[13] = 0x01;
    cid_[14] = 0x40;

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

    const uint32_t read_bl_len = 9u;
    const uint32_t c_size_mult = 4u;
    const uint32_t c_size = static_cast<uint32_t>(std::min<uint64_t>((blocks >> (c_size_mult + 2u)) - 1u, 0x0FFFu));
    put_bits(127, 126, 0u);
    put_bits(125, 122, 4u);
    put_bits(119, 112, 0x0Eu);
    put_bits(103, 96, 0x32u);
    put_bits(95, 84, 0x5B5u);
    put_bits(83, 80, read_bl_len);
    put_bits(73, 62, c_size);
    put_bits(49, 47, c_size_mult);
    put_bits(25, 22, read_bl_len);
    put_bits(14, 14, 1u);

    std::memset(scr_, 0, sizeof(scr_));
    scr_[0] = 0x02;
    scr_[1] = 0x05;
}

void SdCard::BuildExtCsd() {
    std::memset(ext_csd_, 0, sizeof(ext_csd_));
    const uint32_t sectors = static_cast<uint32_t>(media_.Size() / 512u);

    ext_csd_[15] = 0x01;
    ext_csd_[160] = 0x07;
    ext_csd_[162] = 0x00;
    ext_csd_[179] = 0x48;
    ext_csd_[181] = 0x00;
    ext_csd_[183] = 0x00;
    ext_csd_[185] = 0x00;
    ext_csd_[192] = 0x08;
    ext_csd_[194] = 0x02;
    ext_csd_[196] = 0x03;
    ext_csd_[197] = 0x01;
    ext_csd_[199] = 0x01;
    ext_csd_[212] = static_cast<uint8_t>(sectors & 0xFFu);
    ext_csd_[213] = static_cast<uint8_t>((sectors >> 8) & 0xFFu);
    ext_csd_[214] = static_cast<uint8_t>((sectors >> 16) & 0xFFu);
    ext_csd_[215] = static_cast<uint8_t>((sectors >> 24) & 0xFFu);
    ext_csd_[221] = 0x01;
    ext_csd_[222] = 0x01;
    ext_csd_[223] = 0x01;
    ext_csd_[224] = 0x01;
    ext_csd_[225] = 0x01;
    ext_csd_[226] = 0x20;
    ext_csd_[228] = 0x07;
    ext_csd_[494] = 0x01;
    mmc_partition_access_ = ext_csd_[179] & 0x07u;
}

void SdCard::ApplyMmcSwitch(uint32_t arg) {
    const uint8_t access = static_cast<uint8_t>((arg >> 24) & 0x03u);
    const uint8_t index = static_cast<uint8_t>((arg >> 16) & 0xFFu);
    const uint8_t value = static_cast<uint8_t>((arg >> 8) & 0xFFu);

    switch (access) {
    case 0:  break;
    case 1:  ext_csd_[index] = static_cast<uint8_t>(ext_csd_[index] | value); break;
    case 2:  ext_csd_[index] = static_cast<uint8_t>(ext_csd_[index] & ~value); break;
    case 3:  ext_csd_[index] = value; break;
    default: break;
    }

    if (index == 179u)
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
        case 41: {
            r.rsp = Rsp::R3;
            acmd41_done_ = true;
            high_capacity_ = false;
            r.resp[0] = kOcrBusyDone | kOcrVoltageWin;
            if (state_ == State::Idle) state_ = State::Ready;
            return r;
        }
        case 6:  r1(); return r;
        case 42:  r1(); return r;
        case 51:
            r1();
            r.starts_read = true;
            xfer_scr_ = true;
            return r;
        default: break;
        }
    }

    switch (index) {
    case 0:
        state_ = State::Idle;
        acmd41_done_ = false;
        mmc_mode_ = false;
        media_.SetMmcMode(false);
        mmc_predefined_block_count_ = 0;
        mmc_reliable_write_ = false;
        r.rsp = Rsp::None;
        return r;
    case 1: {
        r.rsp = Rsp::R3;
        mmc_mode_ = true;
        media_.SetMmcMode(true);
        if (!mmc_layout_ready_) {
            media_.InitializeMmcLayout();
            mmc_layout_ready_ = true;
        }
        /* sdmemory.dll selects byte or sector command arguments from CMD1 OCR bit 30. */
        high_capacity_ = (arg & kCcsHighCapacity) != 0u;
        r.resp[0] = kOcrBusyDone | kOcrVoltageWin | (high_capacity_ ? kCcsHighCapacity : 0u);
        if (state_ == State::Idle) state_ = State::Ready;
        return r;
    }
    case 2:
        r.rsp = Rsp::R2;
        std::memcpy(r.resp, cid_, 16);
        state_ = State::Ident;
        return r;
    case 3:
        if (mmc_mode_) {
            rca_ = static_cast<uint16_t>((arg >> 16) & 0xFFFFu);
            if (rca_ == 0) rca_ = 0x0001;
            r1();
        } else {
            rca_ = 0x0001;
            r.rsp = Rsp::R6;
            r.resp[0] = (static_cast<uint32_t>(rca_) << 16) | 0x0500;
        }
        state_ = State::Stby;
        return r;
    /* sdmemory.dll uses zero-RCA CMD5 as SDIO IO_SEND_OP_COND, not MMC SLEEP_AWAKE. */
    case 5:
        if (mmc_mode_ && rca_ != 0u && static_cast<uint16_t>((arg >> 16) & 0xFFFFu) == rca_) {
            const bool sleep = ((arg >> 15) & 1u) != 0u;
            if (sleep) state_ = State::Stby;
            r.rsp = Rsp::R1b;
            r.resp[0] = Status();
            return r;
        }
        r.rsp = Rsp::R1;
        r.resp[0] = Status();
        r.illegal = true;
        return r;
    case 6:
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
    case 7:
        r.rsp = Rsp::R1b;
        state_ = (((arg >> 16) & 0xFFFFu) == rca_) ? State::Tran : State::Stby;
        r.resp[0] = Status();
        return r;
    case 8:
        if (mmc_mode_) {
            r1();
            r.starts_read = true;
            xfer_ext_csd_ = true;
            return r;
        }
        r.rsp = Rsp::R7;
        r.resp[0] = arg & 0x00000FFFu;
        return r;
    case 9:
        r.rsp = Rsp::R2;
        std::memcpy(r.resp, csd_, 16);
        return r;
    case 10:
        r.rsp = Rsp::R2;
        std::memcpy(r.resp, cid_, 16);
        return r;
    case 12:
        r.rsp = Rsp::R1b;
        state_ = State::Tran;
        mmc_predefined_block_count_ = 0;
        r.resp[0] = Status();
        return r;
    case 13:
        if (mmc_mode_) media_.Commit();
        r1();
        return r;
    case 15:
        state_ = State::Idle;
        r.rsp = Rsp::None;
        return r;
    case 16:
        blk_len_ = arg ? arg : 512u;
        r1();
        return r;
    case 17:
    case 18:
        r1();
        r.starts_read = true;
        xfer_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        if (mmc_mode_ && index != 18u) mmc_predefined_block_count_ = 0;
        state_ = State::Data;
        return r;
    case 23:
        mmc_predefined_block_count_ = arg & 0x0000FFFFu;
        mmc_reliable_write_ = (arg & 0x80000000u) != 0u;
        r1();
        return r;
    case 24:
    case 25:
        r1();
        r.starts_write = true;
        xfer_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        if (mmc_mode_ && index != 25u) mmc_predefined_block_count_ = 0;
        state_ = State::Rcv;
        return r;
    case 35:
        erase_start_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        erase_start_valid_ = true;
        r1();
        return r;
    case 36:
        erase_end_addr_ = high_capacity_ ? (static_cast<uint64_t>(arg) * 512u) : static_cast<uint64_t>(arg);
        erase_end_valid_ = true;
        r1();
        return r;
    case 38: {
        if (erase_start_valid_ && erase_end_valid_) media_.Erase(erase_start_addr_, erase_end_addr_);
        erase_start_valid_ = false;
        erase_end_valid_ = false;
        r.rsp = Rsp::R1b;
        r.resp[0] = Status();
        state_ = State::Tran;
        return r;
    }
    case 55:  r1(); return r;
    default:
        r.rsp = Rsp::R1;
        r.resp[0] = Status();
        r.illegal = true;
        return r;
    }
}

void SdCard::ReadBlock(uint8_t* dst512) {
    if (xfer_ext_csd_) {
        std::memcpy(dst512, ext_csd_, sizeof(ext_csd_));
        xfer_ext_csd_ = false;
        return;
    }
    if (xfer_scr_) {
        std::memset(dst512, 0, 512u);
        std::memcpy(dst512, scr_, sizeof(scr_));
        xfer_scr_ = false;
        return;
    }
    if (xfer_switch_status_) {
        std::memset(dst512, 0, 512u);
        /* SD Physical Layer Specification Version 8.00 §4.3.10. */
        dst512[0] = 0x00;
        dst512[1] = 0x00;
        dst512[2] = 0x00;
        dst512[3] = 0x03;
        dst512[13] = 0x80;
        dst512[16] = 0x00;
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
