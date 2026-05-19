#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

#define PC_TRACE(addr_, tag_, fmt_)                                   \
    tm.OnPc(addr_, [](const TraceContext& c) {                        \
        LOG(Trace, "[" tag_ "] " fmt_, c.regs[0], c.regs[1],          \
            c.regs[14], c.regs[13]);                                  \
    })

class TraceWm5FilesysInit : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            PC_TRACE(0x1359Cu, "FS_START",
                "filesys start entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1B920u, "FS_1B920",
                "sub_1B920 entered (start->main work) R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1B150u, "FS_1B150",
                "sub_1B150 entered (v8 setter) R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");

            /* sub_1B150 in-function bisect block-starts:
                 0x1B260 — past resource handling, before CreateEventW
                 0x1B2D0 — past LoadLibraryW; only reached if return != 0
                 0x1B3F4 — LABEL_14 bailout entry; sub_1B150 about to return 0 */
            PC_TRACE(0x1B260u, "FS_1B260",
                "post-resource block R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1B2D0u, "FS_1B2D0",
                "LoadLibraryW returned non-zero R4=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1B3F4u, "FS_BAILOUT",
                "sub_1B150 LABEL_14 (return 0) R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");

            /* sub_1B150 bisect calls (in order: sub_3BE7C / sub_1C13C /
               sub_3D678 (FSHEAP init — RETURNS 0 → bailout) / sub_3E3E4
               / sub_3F55C / sub_2EF08 / sub_44604 / sub_257B4 / sub_1A7BC. */
            PC_TRACE(0x3BE7Cu, "FS_3BE7C",
                "sub_3BE7C entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1C13Cu, "FS_1C13C",
                "sub_1C13C entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x3D678u, "FS_FSHEAP_INIT",
                "sub_3D678 entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x3E3E4u, "FS_3E3E4",
                "sub_3E3E4 entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x3F55Cu, "FS_3F55C",
                "sub_3F55C entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x2EF08u, "FS_2EF08",
                "sub_2EF08 entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x44604u, "FS_44604",
                "sub_44604 entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x257B4u, "FS_257B4",
                "sub_257B4 entered (while exit cond) R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1A7BCu, "FS_1A7BC",
                "sub_1A7BC entered (post-while-loop) R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x191C4u, "FS_191C4",
                "sub_191C4 entered phase=R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x196FCu, "FS_LAUNCHER",
                "sub_196FC entered (process launcher) R0(v33_mode)=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");

            /* sub_1C13C internal bisect — does LoadLibraryW(CertMod.dll) return?
                 0x1C148 — first instruction AFTER BL LoadLibraryW
                 0x1C158 — non-null-return branch (calls GetProcAddressW)
                 0x1C1C0 — null-return branch (clean exit, returns 0) */
            PC_TRACE(0x1C148u, "CERTMOD_LL_RET",
                "LoadLibraryW(CertMod.dll) returned R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1C158u, "CERTMOD_LL_NONNULL",
                "LoadLibraryW returned non-null R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x1C1C0u, "CERTMOD_LL_NULL",
                "LoadLibraryW returned 0 R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");

            /* MountSystemHive (sub_26AC0) — calls sub_191C4(0) and (1). */
            PC_TRACE(0x26AC0u, "FS_MSH",
                "MountSystemHive entered R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
            PC_TRACE(0x2775Cu, "FS_2775C",
                "MSH caller block R0=0x%08X R1=0x%08X LR=0x%08X SP=0x%08X\n");
        });
    }
};

#undef PC_TRACE

REGISTER_SERVICE(TraceWm5FilesysInit);

}  /* namespace */

