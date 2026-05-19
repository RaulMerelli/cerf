#include "../../socs/mmu_policy.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"

namespace {

class Sa1110MmuPolicy : public MmuPolicy {
public:
    using MmuPolicy::MmuPolicy;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::Ipaq3650;
    }

    /* SA-1110 Dev Manual §5.2.3: TTBR bits 13:0 ignored on write
       (16-KB L1-table alignment). */
    uint32_t TtbrL1BaseMask() const override { return 0xFFFFC000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Sa1110MmuPolicy, MmuPolicy);
