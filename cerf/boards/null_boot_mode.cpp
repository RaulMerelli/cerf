#include "../boot/boot_mode.h"

#include "board_detector.h"
#include "../core/cerf_emulator.h"

namespace {

/* Fallback boot mode for an unrecognised board, mirroring NullPageTableBuilder:
   returns safe zero entry/stack so cold-boot setup never dereferences an absent
   ROM. The boot doesn't proceed - DeviceNotFoundService / BoardNotFoundService
   run and show the graceful message. */
class NullBootMode : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::Unknown;
    }

    uint32_t ColdEntryPa() override { return 0; }
    uint32_t ColdStackPa() override { return 0; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NullBootMode, BootMode);
