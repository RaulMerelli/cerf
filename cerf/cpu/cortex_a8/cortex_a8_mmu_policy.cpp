#include "../../socs/mmu_policy.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"

namespace {

class CortexA8MmuPolicy : public MmuPolicy {
public:
    using MmuPolicy::MmuPolicy;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::OmapEvm3530;
    }

    uint32_t TtbrL1BaseMask() const override { return 0xFFFFC000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CortexA8MmuPolicy, MmuPolicy);
