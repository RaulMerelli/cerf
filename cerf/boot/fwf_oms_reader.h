#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cerf::fwf_oms {

struct Blob {
    uint32_t attr_id = 0;
    std::string name;
    size_t off = 0;
    size_t size = 0;
};

std::vector<Blob> WalkBlobs(const uint8_t* src, size_t size);

bool IsOmsStream(const uint8_t* src, size_t size);

bool AssembleOsImage(const uint8_t* src, size_t size, std::vector<uint8_t>& out, size_t* slice_count = nullptr);

bool ExtractPersistentStream(const uint8_t* src, size_t size, std::vector<uint8_t>& out);

bool ExtractInstalledFirmwareSummary(const uint8_t* src, size_t size, std::vector<uint8_t>& out);

bool AssembleFsfVolume(const uint8_t* src, size_t size, std::vector<uint8_t>& out);

}
