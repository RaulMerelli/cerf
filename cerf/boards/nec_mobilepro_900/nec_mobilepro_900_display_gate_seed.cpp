#include "../board_detector.h"

#include "../../boot/guest_cold_boot.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../core/service.h"
#include "../../cpu/emulated_memory.h"

#include <cstdint>

namespace {

/* ddi.dll (S1D13806) sub_1C126D0 sizes the primary VRAM allocator from this
   LowMemory gate: 0 caps it at panelHeight+1 (241), too small for the 640x480
   dual-scan surface -> null screen surface -> gwes data-abort. No guest code
   writes it; on hardware the bootloader (CERF replaces it) seeds the region. */
constexpr uint32_t kDisplayGatePa = 0xA001E854u;
constexpr uint32_t kDisplayGateConfigured = 1u;

class NecMobilePro900DisplayGateSeed : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        if (!bd || bd->GetBoard() != Board::NecMobilePro900) return false;
        return !emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        WriteGate();
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { WriteGate(); });
    }

private:
    void WriteGate() {
        emu_.Get<EmulatedMemory>().WriteWord(kDisplayGatePa, kDisplayGateConfigured);
        LOG(Board, "NecMobilePro900DisplayGateSeed: LowMemory+0x1A854 = %u "
                   "at PA 0x%08X\n", kDisplayGateConfigured, kDisplayGatePa);
    }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro900DisplayGateSeed);
