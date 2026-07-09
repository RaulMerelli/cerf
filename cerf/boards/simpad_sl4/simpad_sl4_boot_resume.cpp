#include "../../core/service.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../host/guest_deep_sleep.h"
#include "../../jit/arm/arm_cpu.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* SIMpad's kernel stores its sleep-resume vector in PSPR (power mgr +0x08;
   nk.exe `start` writes 0x90020008). On an SMR wake CERF resumes the guest at
   that vector, skipping the cold-entry head-copy that overwrites the save block. */
class SimpadSl4BootResume : public Service, public SleepResumeVectorProvider {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }
    void OnReady() override {
        emu_.Get<GuestDeepSleep>().RegisterResumeVectorProvider(this);
    }

    void ApplyPendingResume() override {
        /* PSPR reads 0 until the kernel stores its vector, and
           SetPendingResumeVector arms the reset PC unconditionally. The kernel's
           resume entry runs MMU-off and re-enables the MMU itself. */
        const uint32_t pc = emu_.Get<PeripheralDispatcher>().ReadWord(0x90020008u);
        if (pc) emu_.Get<ArmCpu>().SetPendingResumeVector(pc);
    }
};

}  /* namespace */

REGISTER_SERVICE(SimpadSl4BootResume);
