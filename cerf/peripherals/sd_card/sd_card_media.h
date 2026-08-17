#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

class SdCardMedia {
public:
    explicit SdCardMedia(uint64_t size_bytes);
    ~SdCardMedia();

    uint64_t Size() const { return data_.size(); }
    void SetHardwareMac(const std::array<uint8_t, 6>& mac);
    void SetMmcMode(bool enabled) { persistent_ = enabled; }
    void InitializeMmcLayout();
    void Erase(uint64_t first_address, uint64_t last_address);
    void Read(uint64_t offset, uint8_t* dst512) const;
    void Write(uint64_t offset, const uint8_t* src512);
    void Commit();

private:
    void BuildMinimalFat16();
    void BuildBlankPdcfsPartition();
    void EnsureKtp400PdcfsRootDirs();
    void EnsureKtp400HardwareInfoSeed();
    void PersistBackingRange(uint64_t offset, uint64_t length);
    void MarkDirtyBackingRange(uint64_t offset, uint64_t length);
    void FlushDirtyBackingRanges();
    void PersistKtp400HardwareInfoSeed();
    bool OverlapsKtp400FactoryArea(uint64_t offset, uint64_t length) const;

    bool persistent_ = false;
    bool layout_initialized_ = false;
    std::vector<uint8_t> data_;
    std::vector<uint8_t> fwf_info_stream_;
    std::vector<uint8_t> fwf_info_fallback_object_;
    std::vector<std::pair<uint64_t, uint64_t>> dirty_ranges_;
    uint64_t dirty_bytes_pending_ = 0;
    std::array<uint8_t, 6> hardware_mac_ = {
        0x02u, 0xCEu, 0x5Fu, 0x00u, 0x00u, 0x01u
    };
};
