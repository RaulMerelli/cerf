#pragma once

#include <array>
#include <cstdint>
#include <vector>

std::vector<uint8_t> BuildKtp400HardwareInfoOms(
    const std::array<uint8_t, 6>& mac);
