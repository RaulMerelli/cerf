#pragma once

#include <cstdint>
#include <ctime>

namespace cerf::rtc_bcd {

inline std::tm LocalTime(std::time_t value) {
    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

inline uint8_t BinToBcd(int value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

inline bool BcdToBin(uint8_t value, uint8_t mask, int maximum, int& result) {
    const int masked = value & mask;
    const int high = (masked >> 4) & 0x0F;
    const int low = masked & 0x0F;
    if (high > 9 || low > 9) return false;
    result = high * 10 + low;
    return result <= maximum;
}

}
