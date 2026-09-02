#pragma once

#include "ktp_mobile_hardware_info.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class SdCardMedia {
public:
    explicit SdCardMedia(uint64_t size_bytes);
    ~SdCardMedia();

    uint64_t Size() const { return data_.size(); }
    void ConfigureKtp400(const std::string& device_dir, const std::string& container_name, KtpMobileOpType op_type,
                         KtpMobilePanel panel, const std::array<uint8_t, 6>& mac);
    void SetMmcMode(bool enabled) { persistent_ = enabled && ktp400_configured_; }
    void InitializeMmcLayout();
    void Erase(uint64_t first_address, uint64_t last_address);
    /* false = the offset is past the media; SD Physical Layer
       Simplified Spec 3.01 Card Status bit 31 OUT_OF_RANGE (p. 75). */
    bool Read(uint64_t offset, uint8_t* dst512) const;
    bool Write(uint64_t offset, const uint8_t* src512);
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

    bool persistent_ = false;
    bool ktp400_configured_ = false;
    bool layout_initialized_ = false;
    std::string backing_path_;
    std::vector<uint8_t> data_;
    std::vector<uint8_t> fwf_container_;
    KtpMobileOpType op_type_ = KtpMobileOpType::Ktp400F;
    KtpMobilePanel panel_{480u, 272u};
    std::vector<std::pair<uint64_t, uint64_t>> dirty_ranges_;
    uint64_t dirty_bytes_pending_ = 0;
    std::array<uint8_t, 6> hardware_mac_ = {0x02u, 0xCEu, 0x5Fu, 0x00u, 0x00u, 0x01u};
};
