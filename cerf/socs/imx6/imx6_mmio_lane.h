#pragma once

#include "../../core/log.h"

#include <cstdint>

struct Imx6MmioLane {
    uint32_t address;
    uint32_t value;
    uint32_t mask;

    uint32_t Merge(uint32_t current) const {
        return (current & ~mask) | (value & mask);
    }
};

template <typename ReadWord>
uint8_t Imx6ReadMmioByte(uint32_t address, ReadWord read_word) {
    const uint32_t shift = (address & 3u) * 8u;
    return static_cast<uint8_t>(read_word(address & ~3u) >> shift);
}

template <typename ReadWord>
uint16_t Imx6ReadMmioHalf(uint32_t address, ReadWord read_word) {
    const uint32_t shift = (address & 3u) * 8u;
    const uint32_t low = read_word(address & ~3u) >> shift;
    if (shift <= 16u) return static_cast<uint16_t>(low);
    return static_cast<uint16_t>(low | (read_word((address & ~3u) + 4u) << 8u));
}

inline Imx6MmioLane Imx6DecodeMmioLane(uint32_t address, uint32_t value,
                                       uint32_t width) {
    if (width != 1u && width != 2u) {
        LOG(Caution, "i.MX6 MMIO lane: width %u is not modelled\n", width);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t shift = (address & 3u) * 8u;
    const uint32_t lane_mask = width == 1u ? 0xFFu : 0xFFFFu;
    return {address & ~3u, (value & lane_mask) << shift, lane_mask << shift};
}

template <typename Callback>
void Imx6ForEachMmioLane(uint32_t address, uint32_t value, uint32_t width,
                         Callback callback) {
    const uint32_t first_width = (width == 2u && (address & 3u) == 3u) ? 1u : width;
    callback(Imx6DecodeMmioLane(address, value, first_width));
    if (first_width != width)
        callback(Imx6DecodeMmioLane(address + first_width,
                                    value >> (first_width * 8u),
                                    width - first_width));
}

template <typename PeripheralType>
void Imx6MergeMmioWrite(PeripheralType& peripheral, uint32_t address,
                        uint32_t value, uint32_t width) {
    Imx6ForEachMmioLane(address, value, width,
        [&peripheral](const Imx6MmioLane& lane) {
            peripheral.WriteWord(lane.address,
                                 lane.Merge(peripheral.ReadWord(lane.address)));
        });
}
