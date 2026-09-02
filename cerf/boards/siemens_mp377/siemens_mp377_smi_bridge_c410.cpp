#include "siemens_mp377_sm501_internal.h"
#include "siemens_mp377_smi_bridge_c410.h"

#include "../../core/cerf_emulator.h"

namespace siemens_mp377 {

uint32_t SiemensMp377SmiBridgeC410::MmioBase() const {
    return SmiBridgeBase(Mp377SmiBridgeWindowId::C410);
}

uint32_t SiemensMp377SmiBridgeC410::MmioSize() const {
    return kSmiBridgeWindowBytes;
}

bool SiemensMp377SmiBridgeC410::IsC410ConsoleAlias() const {
    return true;
}

void SiemensMp377SmiBridgeC410::SaveState(StateWriter& w) {
    emu_.Get<SiemensMp377SmiBridge>().SaveState(w);
}

void SiemensMp377SmiBridgeC410::RestoreState(StateReader& r) {
    emu_.Get<SiemensMp377SmiBridge>().RestoreState(r);
}

void SiemensMp377SmiBridgeC410::PostRestore() {
    emu_.Get<SiemensMp377SmiBridge>().PostRestoreState();
}

REGISTER_SERVICE(SiemensMp377SmiBridgeC410);

} // namespace siemens_mp377
