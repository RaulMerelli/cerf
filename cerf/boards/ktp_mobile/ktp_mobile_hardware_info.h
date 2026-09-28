#pragma once

#include <array>
#include <cstdint>
#include <vector>

enum class KtpMobileOpType : uint16_t {
    Ktp400F = 0x0001,
    Ktp700 = 0x0002,
    Ktp700F = 0x0004,
    Ktp900 = 0x0008,
    Ktp900F = 0x0010,
    Tp1000F = 0x0040,
    Tp1000FRo = 0x0080,
    Ktp700FHw = 0x0100,
    Ktp700FArctic = 0x0200,
};

struct KtpMobilePanel {
    uint16_t width;
    uint16_t height;
    uint16_t hsync_width;
    uint16_t hstart_width;
    uint16_t hend_width;
    uint16_t vsync_width;
    uint16_t vstart_width;
    uint16_t vend_width;
    uint32_t pixel_clock_hz;
    uint8_t  data_bus_width;
};

std::vector<uint8_t> BuildKtpMobileHardwareInfoOms(const std::array<uint8_t, 6>& mac, KtpMobileOpType op_type,
                                                   KtpMobilePanel panel);

std::vector<uint8_t> BuildKtpMobileInstalledHardwareDescriptionOms(const std::array<uint8_t, 6>& mac,
                                                                   KtpMobileOpType op_type,
                                                                   KtpMobilePanel panel);
