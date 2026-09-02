#pragma once

#include "fwf_fsf_container.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace ktp_mobile_emmc {

using PersistRange = std::function<void(uint64_t offset, uint64_t length)>;

void EnsurePdcfsLayout(std::vector<uint8_t>& data, const PersistRange& persist_range,
                       const std::vector<fwf_fsf::FsfEntry>& addon_files);

} // namespace ktp_mobile_emmc
