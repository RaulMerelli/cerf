#include "sd_card.h"
#include "../../core/log.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t kCcsHighCapacity = 1u << 30;
constexpr uint32_t kOcrBusyDone = 1u << 31;
constexpr uint32_t kOcrVoltageWin = 0x00FF8000u;
constexpr uint32_t kStatusOutOfRange = 1u << 31;

void WriteBe16(uint8_t* at, uint16_t value) {
    at[0] = static_cast<uint8_t>(value >> 8);
    at[1] = static_cast<uint8_t>(value);
}
/* QEMU sd.c: CSR field SWITCH_ERROR is bit 7. */
constexpr uint32_t kStatusSwitchError = 1u << 7;
/* QEMU sd.c emmc_function_switch: EXT_CSD below 192 is writable, at or above it is not. */
constexpr uint8_t kExtCsdReadOnlyBase = 192u;
}

SdCard::SdCard(uint64_t size_bytes) : media_(size_bytes) {
    BuildCidCsd();
    BuildExtCsd();
}

void SdCard::ConfigureMedia(std::unique_ptr<SdCardMediaBackend> backend) {
    media_.Configure(std::move(backend));
}

void SdCard::SaveState(StateWriter& w) const {
    w.Write("state", state_);
    w.Write("rca", rca_);
    w.Write("card_status", card_status_);
    w.Write("high_capacity", high_capacity_);
    w.Write("xfer_addr", xfer_addr_);
    w.WriteBytes("ext_csd", ext_csd_, sizeof(ext_csd_));
    w.Write("xfer_scr", xfer_scr_);
    w.Write("xfer_switch_status", xfer_switch_status_);
    w.Write("switch_status_arg", switch_status_arg_);
    w.Write("mmc_mode", mmc_mode_);
    w.Write("mmc_layout_ready", mmc_layout_ready_);
    w.Write("xfer_ext_csd", xfer_ext_csd_);
    w.Write("mmc_predefined_block_count", mmc_predefined_block_count_);
    w.Write("erase_start_addr", erase_start_addr_);
    w.Write("erase_end_addr", erase_end_addr_);
    w.Write("erase_start_valid", erase_start_valid_);
    w.Write("erase_end_valid", erase_end_valid_);
}

void SdCard::RestoreState(StateReader& r) {
    r.Read("state", state_);
    r.Read("rca", rca_);
    r.Read("card_status", card_status_);
    r.Read("high_capacity", high_capacity_);
    r.Read("xfer_addr", xfer_addr_);
    r.ReadBytes("ext_csd", ext_csd_, sizeof(ext_csd_));
    r.Read("xfer_scr", xfer_scr_);
    r.Read("xfer_switch_status", xfer_switch_status_);
    r.Read("switch_status_arg", switch_status_arg_);
    r.Read("mmc_mode", mmc_mode_);
    r.Read("mmc_layout_ready", mmc_layout_ready_);
    r.Read("xfer_ext_csd", xfer_ext_csd_);
    r.Read("mmc_predefined_block_count", mmc_predefined_block_count_);
    r.Read("erase_start_addr", erase_start_addr_);
    r.Read("erase_end_addr", erase_end_addr_);
    r.Read("erase_start_valid", erase_start_valid_);
    r.Read("erase_end_valid", erase_end_valid_);
    if (mmc_layout_ready_) media_.InitializeMmcLayout();
    media_.SetMmcMode(mmc_mode_);
}

/* SD Physical Layer Simplified Specification 3.01 §5.2 (CID), §5.3.2 (CSD Version 1.0), §5.6 (SCR). */
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
    /* QEMU sd.c emmc_set_csd: CSD_STRUCTURE 3 with SPEC_VERS 4. */
    put_bits(127, 126, 3u);
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
    /* QEMU sd.c emmc_set_ext_csd names the EXT_CSD properties-segment fields. */
    constexpr uint32_t kPartitionSupport = 160u;
    constexpr uint32_t kPartConfig = 179u;
    constexpr uint32_t kRev = 192u;
    constexpr uint32_t kStructure = 194u;
    constexpr uint32_t kCardType = 196u;
    constexpr uint32_t kDriverStrength = 197u;
    constexpr uint32_t kPartSwitchTime = 199u;
    constexpr uint32_t kSecCnt = 212u;
    constexpr uint32_t kHcWpGrpSize = 221u;
    constexpr uint32_t kRelWrSecC = 222u;
    constexpr uint32_t kEraseTimeoutMult = 223u;
    constexpr uint32_t kHcEraseGrpSize = 224u;
    constexpr uint32_t kAccSize = 225u;
    constexpr uint32_t kBootMult = 226u;
    constexpr uint32_t kBootInfo = 228u;

    std::memset(ext_csd_, 0, sizeof(ext_csd_));
    const uint32_t sectors = static_cast<uint32_t>(media_.Size() / 512u);

    ext_csd_[kPartitionSupport] = 0x07;
    /* QEMU sd.c masks PART_CONFIG with ACC_MASK; 0 selects the user area. */
    ext_csd_[kPartConfig] = 0x48;
    ext_csd_[kRev] = 0x08;
    ext_csd_[kStructure] = 0x02;
    /* QEMU sd.c emmc_set_ext_csd: CARD_TYPE 0x03 is the 26 and 52 MHz pair. */
    ext_csd_[kCardType] = 0x03;
    ext_csd_[kDriverStrength] = 0x01;
    ext_csd_[kPartSwitchTime] = 0x01;
    ext_csd_[kSecCnt] = static_cast<uint8_t>(sectors & 0xFFu);
    ext_csd_[kSecCnt + 1u] = static_cast<uint8_t>((sectors >> 8) & 0xFFu);
    ext_csd_[kSecCnt + 2u] = static_cast<uint8_t>((sectors >> 16) & 0xFFu);
    ext_csd_[kSecCnt + 3u] = static_cast<uint8_t>((sectors >> 24) & 0xFFu);
    ext_csd_[kHcWpGrpSize] = 0x01;
    ext_csd_[kRelWrSecC] = 0x01;
    ext_csd_[kEraseTimeoutMult] = 0x01;
    ext_csd_[kHcEraseGrpSize] = 0x01;
    ext_csd_[kAccSize] = 0x01;
    ext_csd_[kBootMult] = 0x20;
    ext_csd_[kBootInfo] = 0x07;
}

void SdCard::ApplyMmcSwitch(uint32_t arg) {
    const uint8_t access = static_cast<uint8_t>((arg >> 24) & 0x03u);
    const uint8_t index = static_cast<uint8_t>((arg >> 16) & 0xFFu);
    const uint8_t value = static_cast<uint8_t>((arg >> 8) & 0xFFu);

    if (index >= kExtCsdReadOnlyBase) {
        card_status_ |= kStatusSwitchError;
        return;
    }

    switch (access) {
    case 0:
        /* QEMU sd.c emmc_function_switch: access mode 0 switches the command set. */
        LOG(Caution, "SdCard: CMD6 command-set switch to 0x%02X is not modelled\n", value);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    case 1:  ext_csd_[index] = static_cast<uint8_t>(ext_csd_[index] | value); break;
    case 2:  ext_csd_[index] = static_cast<uint8_t>(ext_csd_[index] & ~value); break;
    case 3:  ext_csd_[index] = value; break;
    }

    /* QEMU sd.c redirects the address window by PART_CONFIG ACC; only the
       user area is modelled here. */
    if (index == 179u && (ext_csd_[179] & 0x07u) != 0u) {
        LOG(Caution, "SdCard: PART_CONFIG access 0x%02X is not modelled\n",
            ext_csd_[179] & 0x07u);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
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
        /* QEMU sd.c returns sd_illegal for an application command it does not implement. */
        default:
            r1();
            r.illegal = true;
            return r;
        }
    }

    switch (index) {
    case 0:
        state_ = State::Idle;
        mmc_mode_ = false;
        media_.SetMmcMode(false);
        mmc_predefined_block_count_ = 0;
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
        switch_status_arg_ = arg;
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
        /* ReadBlock and WriteBlock move 512 bytes; SD Physical Layer Simplified
           Specification 3.01 §4.3.2 makes 512 the only length a HC card accepts. */
        if (arg != 0u && arg != 512u) {
            LOG(Caution, "SdCard: CMD16 block length %u is not modelled\n", arg);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
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
        /* SD Physical Layer Simplified Specification 3.01 Table 4-11: 511:496 maximum current in
           mA with 0 meaning Error, one 16-bit support bitmap per group from 415:400 for group 1
           to 495:480 for group 6, one result nibble per group from 379:376, 375:368 version. */
        constexpr uint32_t kMaxCurrentMilliamps = 1u;
        constexpr uint16_t kDefaultFunctionOnly = 0x0001u;
        constexpr uint32_t kGroups = 6u;
        constexpr uint32_t kFunctionSetError = 0x0Fu;

        WriteBe16(dst512, static_cast<uint16_t>(kMaxCurrentMilliamps));
        for (uint32_t group = 1u; group <= kGroups; ++group) {
            const uint32_t high_bit = 399u + 16u * group;
            WriteBe16(dst512 + (511u - high_bit) / 8u, kDefaultFunctionOnly);

            const uint32_t result_high_bit = 376u + (group - 1u) * 4u + 3u;
            const uint32_t byte = (511u - result_high_bit) / 8u;
            const uint32_t shift = ((511u - result_high_bit) % 8u) == 0u ? 4u : 0u;
            const uint32_t requested = (switch_status_arg_ >> ((group - 1u) * 4u)) & 0x0Fu;
            const uint32_t granted = (requested == 0u || requested == 0x0Fu)
                                         ? 0u
                                         : kFunctionSetError;
            dst512[byte] |= static_cast<uint8_t>(granted << shift);
        }
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
