#pragma once

#include "ktp_mobile_fwf_fsf_container.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace ktp_mobile_emmc {

using PersistRange = std::function<void(uint64_t offset, uint64_t length)>;

bool EnsurePdcfsLayout(std::vector<uint8_t>& data, const PersistRange& persist_range,
                       const std::vector<ktp_mobile_fwf::FsfEntry>& addon_files);

}
