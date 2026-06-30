#include "../../core/service.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../host/guest_deep_sleep.h"
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

    SleepResumeState Resume() override {
        /* PSPR holds the kernel `start` post-head-copy entry, which runs MMU-off
           and re-enables the MMU itself, so no cp15 restore here. */
        return { emu_.Get<PeripheralDispatcher>().ReadWord(0x90020008u),
                 /*restore_mmu=*/false };
    }
};

}  /* namespace */

REGISTER_SERVICE(SimpadSl4BootResume);
