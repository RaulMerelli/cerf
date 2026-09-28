#pragma once

#include "ktp_mobile_emmc_backing.h"
#include "ktp_mobile_hardware_info.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class KtpMobileSdCardBackend final : public KtpMobileEmmcBacking {
public:
    KtpMobileSdCardBackend(std::string backing_path, std::string device_dir,
                           std::string container_name, KtpMobileOpType op_type,
                           KtpMobilePanel panel, std::array<uint8_t, 6> mac);

    void Initialize(std::vector<uint8_t>& data) override;
    void Persist(const std::vector<uint8_t>& data, uint64_t offset,
                 uint64_t length) override;
    void Flush(const std::vector<uint8_t>& data) override;

private:
    void EnsureHardwareInfo(std::vector<uint8_t>& data);
    void PersistHardwareInfo(const std::vector<uint8_t>& data);
    void MarkDirty(uint64_t offset, uint64_t length, uint64_t size);

    std::string backing_path_;
    std::vector<uint8_t> fwf_container_;
    KtpMobileOpType op_type_;
    KtpMobilePanel panel_;
    std::array<uint8_t, 6> hardware_mac_;
    std::vector<std::pair<uint64_t, uint64_t>> dirty_ranges_;
    uint64_t dirty_bytes_pending_ = 0;
};
