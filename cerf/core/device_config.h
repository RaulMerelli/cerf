#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct DeviceMeta {
    std::string device_name;
    std::string board_name;
    std::string soc_family;
    std::string os_name;
    int         os_ver_major = 0;
    int         os_ver_minor = 0;
    int         device_year  = 0;
};

struct DeviceConfig {
    std::string device_name;

    DeviceMeta meta;

    std::optional<uint32_t> board_configurable_screen_width;
    std::optional<uint32_t> board_configurable_screen_height;

    bool        network_enabled = true;
    std::string network_mac     = "02:CE:5F:00:00:01";
    uint32_t    network_mtu     = 1500;
    std::string network_forward_tcp;
    std::string network_forward_udp;

    std::string              rom_primary;
    std::vector<std::string> rom_extensions;
    std::string              rom_recovery;

    bool poc_rom_injection = false;
};
