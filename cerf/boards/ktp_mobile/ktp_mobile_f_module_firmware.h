#pragma once

#include "ktp_mobile_f_module_model.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ktp_mobile::detail {

inline constexpr std::array<std::uint8_t, 20> kContainerIdentity{{
    'K', 'T', 'P', '_', 'M', 'O', 'B', 'I', 'L', 'E',
    '_', 'F', 'M', 'O', 'D', 'U', 'L', 'E', ' ', ' '
}};

struct ParsedContainer {
    std::size_t payload_size = 0u;
    std::size_t version_offset = 0u;
};

std::uint16_t Crc16(const std::uint8_t* data, std::size_t length) noexcept;
std::array<std::uint8_t, 32> Sha256(const std::uint8_t* data,
                                    std::size_t length) noexcept;
bool ParseContainerStructure(const std::uint8_t* data, std::size_t length,
                             ParsedContainer& parsed) noexcept;

}
