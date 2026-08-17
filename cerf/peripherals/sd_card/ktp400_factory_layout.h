#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ktp400_emmc {

struct FwfAssets {
    std::vector<uint8_t> info_stream;
    std::vector<uint8_t> fallback_object;
};

FwfAssets LoadFwfAssets();

void EnsureFactoryLayout(std::vector<uint8_t>& data,
                         const std::vector<uint8_t>& fwf_info_stream,
                         const std::vector<uint8_t>& fwf_info_fallback_object,
                         const std::array<uint8_t, 6>& hardware_mac);

}
