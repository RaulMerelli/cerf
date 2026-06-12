#include "../mmu_policy.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"

namespace {

class CortexA8MmuPolicy : public MmuPolicy {
public:
    using MmuPolicy::MmuPolicy;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::OMAP3530 || soc == SocFamily::iMX51;
    }

    uint32_t TtbrL1BaseMask() const override { return 0xFFFFC000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CortexA8MmuPolicy, MmuPolicy);
