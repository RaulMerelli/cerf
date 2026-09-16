#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ktp_mobile_fwf {

struct FsfEntry {
    std::string dir;
    std::string name;
    std::vector<uint8_t> data;
};

std::vector<FsfEntry> ParseFsfVolume(const std::vector<uint8_t>& fwf_stream);

struct FatSink {
    uint32_t cluster_bytes = 0;
    std::function<uint32_t()> alloc_cluster;
    std::function<void(uint32_t, uint32_t)> link_fat;
    std::function<uint8_t*(uint32_t)> cluster_ptr;
    std::function<void(uint32_t, uint32_t, uint32_t)> persist;
};

uint32_t SeedFsfVolume(const std::vector<FsfEntry>& entries, uint32_t root_clus, const FatSink& fat);

}
