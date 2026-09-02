#pragma once

#include "ktp_mobile_hardware_info.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ktp_mobile_emmc {

std::vector<uint8_t> LoadFwfContainer(const std::string& device_dir, const std::string& container_name);

void EnsureFactoryLayout(std::vector<uint8_t>& data, const std::vector<uint8_t>& fwf_container,
                         const std::array<uint8_t, 6>& hardware_mac, KtpMobileOpType op_type, KtpMobilePanel panel);

} // namespace ktp_mobile_emmc
