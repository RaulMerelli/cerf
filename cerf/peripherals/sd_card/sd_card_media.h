#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sd_card_media_backend.h"

class SdCardMedia {
public:
    explicit SdCardMedia(uint64_t size_bytes);
    ~SdCardMedia();

    uint64_t Size() const { return data_.size(); }
    void Configure(std::unique_ptr<SdCardMediaBackend> backend);
    void SetMmcMode(bool enabled) { persistent_ = enabled && backend_ != nullptr; }
    void InitializeMmcLayout();
    void Erase(uint64_t first_address, uint64_t last_address);
    bool Read(uint64_t offset, uint8_t* dst512) const;
    bool Write(uint64_t offset, const uint8_t* src512);
    void Commit();

private:
    void BuildMinimalFat16();
    bool persistent_ = false;
    bool layout_initialized_ = false;
    std::vector<uint8_t> data_;
    std::unique_ptr<SdCardMediaBackend> backend_;
};
