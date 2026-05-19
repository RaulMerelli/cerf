#include "../mmu_policy.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"

namespace {

class S3C2410MmuPolicy : public MmuPolicy {
public:
    using MmuPolicy::MmuPolicy;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetSoc() == SocFamily::S3C2410;
    }

    uint32_t TtbrL1BaseMask() const override { return 0xFFFFF000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(S3C2410MmuPolicy, MmuPolicy);
