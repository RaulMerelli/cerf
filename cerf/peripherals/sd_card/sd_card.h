#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sd_card_media.h"
#include "sd_card_media_backend.h"

class StateReader;
class StateWriter;

class SdCard {
public:
    enum class Rsp : uint8_t { None, R1, R1b, R2, R3, R6, R7 };

    struct CommandResult {
        Rsp rsp = Rsp::None;
        uint32_t resp[4] = {0, 0, 0, 0};
        bool illegal = false;
        bool starts_read = false;
        bool starts_write = false;
    };

    explicit SdCard(uint64_t size_bytes);
    ~SdCard() = default;

    CommandResult Command(uint8_t index, uint32_t arg, bool app_cmd);

    void ReadBlock(uint8_t* dst512);
    void WriteBlock(const uint8_t* src512);
    void CommitWrites();
    void ConfigureMedia(std::unique_ptr<SdCardMediaBackend> backend);
    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);


private:
    enum class State : uint8_t { Idle, Ready, Ident, Stby, Tran, Data, Rcv };

    void BuildCidCsd();
    void BuildExtCsd();
    void ApplyMmcSwitch(uint32_t arg);
    uint32_t Status() const;

    State state_ = State::Idle;
    uint16_t rca_ = 0;
    mutable uint32_t card_status_ = 0;
    bool high_capacity_ = false;
    uint64_t xfer_addr_ = 0;
    uint8_t cid_[16] = {0};
    uint8_t csd_[16] = {0};
    uint8_t scr_[8] = {0};
    uint8_t ext_csd_[512] = {0};
    bool xfer_scr_ = false;
    bool xfer_switch_status_ = false;
    uint32_t switch_status_arg_ = 0;
    bool mmc_mode_ = false;
    bool mmc_layout_ready_ = false;
    bool xfer_ext_csd_ = false;
    uint32_t mmc_predefined_block_count_ = 0;
    uint64_t erase_start_addr_ = 0;
    uint64_t erase_end_addr_ = 0;
    bool erase_start_valid_ = false;
    bool erase_end_valid_ = false;
    SdCardMedia media_;
};
