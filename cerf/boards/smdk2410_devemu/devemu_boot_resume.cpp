#include "../../core/service.h"

#include "../../boards/board_detector.h"
#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/guest_deep_sleep.h"

#include <cstdint>

namespace {

/* SLEEPDATA (VA 0xA0020800) holds the resume routine VA + cp15 c1/c2/c3 that
   CPUPowerOff saves and a power-off wake restores before jumping to it with the
   MMU on (cpupoweroff.s, EBOOT startup.s wakeup routine). */
constexpr uint32_t kSleepDataUncachedVa = 0xA0020800u;

/* SLEEPDATA word offsets (cpupoweroff.s SleepState_*). */
constexpr uint32_t kOffWakeAddr  = 0x00u;   /* resume routine VA (Awake_address) */
constexpr uint32_t kOffMmuCtl    = 0x04u;   /* cp15 c1 control                   */
constexpr uint32_t kOffMmuTtb    = 0x08u;   /* cp15 c2 translation table base    */
constexpr uint32_t kOffMmuDomain = 0x0Cu;   /* cp15 c3 domain access control     */

class DevEmuBootResume : public Service, public SleepResumeVectorProvider {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }
    void OnReady() override {
        emu_.Get<GuestDeepSleep>().RegisterResumeVectorProvider(this);
    }

    SleepResumeState Resume() override {
        /* The uncached alias (0xAxxxxxxx) and cached alias (0x8xxxxxxx) resolve to
           the same DRAM PA. SLEEPDATA at 0xA0020800 is the Bowmore/CE6 BSP layout;
           a devemu kernel mapped elsewhere (NOR-flash UI image) has no SLEEPDATA
           band here, so pa==0 means no resume model applies. */
        const uint32_t cached_va = kSleepDataUncachedVa & ~0x20000000u;
        uint32_t pa = 0u;
        for (const auto& r : emu_.Get<PageTableBuilder>().CachedDramRegions()) {
            if (cached_va >= r.va_base && cached_va < r.va_base + r.size) {
                pa = r.pa_base + (cached_va - r.va_base);
                break;
            }
        }
        if (pa == 0u) {
            LOG(SocReset, "DevEmuBootResume: SLEEPDATA VA 0x%08X not in cached DRAM "
                "-> cold boot\n", cached_va);
            return {};
        }
        auto& mem = emu_.Get<EmulatedMemory>();

        const uint32_t wake_addr = mem.ReadWord(pa + kOffWakeAddr);
        if (wake_addr == 0u) {
            /* CPUPowerReset stores WakeAddr=0; EBOOT then cold-boots (startup.s). */
            LOG(SocReset, "DevEmuBootResume: SLEEPDATA WakeAddr=0 -> cold boot\n");
            return {};
        }
        const uint32_t mmu_ctl = mem.ReadWord(pa + kOffMmuCtl);
        const uint32_t mmu_ttb = mem.ReadWord(pa + kOffMmuTtb);
        const uint32_t mmu_dom = mem.ReadWord(pa + kOffMmuDomain);
        LOG(SocReset, "DevEmuBootResume: resume PC=0x%08X ctl=0x%08X ttb=0x%08X "
            "dacr=0x%08X (SLEEPDATA pa=0x%08X)\n",
            wake_addr, mmu_ctl, mmu_ttb, mmu_dom, pa);
        return { wake_addr, /*restore_mmu=*/true, mmu_ctl, mmu_ttb, mmu_dom };
    }
};

}  /* namespace */

REGISTER_SERVICE(DevEmuBootResume);
