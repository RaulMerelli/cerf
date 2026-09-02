#include "siemens_mp377_sm501_internal.h"
#include "siemens_mp377_smi_bridge_c480.h"
#include "../../core/cerf_emulator.h"

namespace siemens_mp377 {

uint32_t SiemensMp377SmiBridgeC480::MmioBase() const {
    return SmiBridgeBase(Mp377SmiBridgeWindowId::C480);
}

uint32_t SiemensMp377SmiBridgeC480::MmioSize() const {
    return kSmiBridgeWindowBytes;
}

REGISTER_SERVICE(SiemensMp377SmiBridgeC480);

} // namespace siemens_mp377
