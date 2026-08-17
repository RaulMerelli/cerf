#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace ktp400_emmc {

using PersistRange = std::function<void(uint64_t offset, uint64_t length)>;

void EnsurePdcfsLayout(std::vector<uint8_t>& data,
                       const PersistRange& persist_range);

}
