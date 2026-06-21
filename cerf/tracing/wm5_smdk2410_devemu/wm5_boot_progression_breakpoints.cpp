#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

constexpr uint32_t kPcNkStart              = 0x80076CF0u;  /* nk.exe   start */
constexpr uint32_t kPcFilesysStart         = 0x0001359Cu;  /* filesys  start */
constexpr uint32_t kPcGwesStart            = 0x00016DECu;  /* gwes     start */
constexpr uint32_t kPcWelcomeStart         = 0x00011240u;  /* welcome  start */
constexpr uint32_t kPcCoredllCreateProcW   = 0x01F7B108u;  /* CreateProcessW   */
constexpr uint32_t kPcCoredllCreateThread  = 0x01F7B1FCu;  /* CreateThread     */
constexpr uint32_t kPcCoredllLoadLibraryEx = 0x01F779DCu;  /* LoadLibraryExW   */

class TraceWm5BootProgressionBreakpoints : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            auto hit = [](const char* tag) {
                return [tag](const TraceContext& c) {
                    LOG(Trace,
                        "[BOOT_HIT_%s] PC=0x%08X "
                        "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                        "LR=0x%08X SP=0x%08X CPSR=0x%08X\n",
                        tag, c.pc,
                        c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                        c.regs[14], c.regs[13], c.cpsr);
                };
            };
            tm.OnPc(kPcNkStart,              hit("NK_START"));
            tm.OnPc(kPcFilesysStart,         hit("FILESYS_START"));
            tm.OnPc(kPcGwesStart,            hit("GWES_START"));
            tm.OnPc(kPcWelcomeStart,         hit("WELCOME_START"));
            tm.OnPc(kPcCoredllCreateProcW,   hit("CreateProcessW"));
            tm.OnPc(kPcCoredllCreateThread,  hit("CreateThread"));
            tm.OnPc(kPcCoredllLoadLibraryEx, hit("LoadLibraryExW"));

            /* Inside-welcome.exe milestones - fires when welcome.exe
               executes specific PCs. RVAs from IDA of welcome.exe.
               Slot-folded JIT VA == RVA (welcome.exe loaded at slot
               base + RVA, start = 0x11240 < 32 MB so FCSE-folded). */
            tm.OnPc(0x00011E34u, hit("WEL_sub_11E34"));      /* init func    */
            tm.OnPc(0x00011BC0u, hit("WEL_DialogProc"));     /* dlg messages */
            tm.OnPc(0x00014E4Cu, hit("WEL_CreateWindowEx")); /* thunk        */
            tm.OnPc(0x00014D6Cu, hit("WEL_DialogBoxInd"));   /* thunk        */
            tm.OnPc(0x00014F5Cu, hit("WEL_BitBlt"));         /* thunk        */
            tm.OnPc(0x00014CCCu, hit("WEL_StretchBlt"));     /* thunk        */
            tm.OnPc(0x00014F6Cu, hit("WEL_DrawTextW"));      /* thunk        */
            tm.OnPc(0x00014C7Cu, hit("WEL_LoadStringW"));    /* thunk        */
            tm.OnPc(0x00014E8Cu, hit("WEL_LoadLibraryExW")); /* thunk        */
            tm.OnPc(0x00014D8Cu, hit("WEL_FindResourceW"));  /* thunk        */
            tm.OnPc(0x00014D7Cu, hit("WEL_LoadResource"));   /* thunk        */
            tm.OnPc(0x00014E2Cu, hit("WEL_sndPlaySoundW"));  /* thunk        */
            tm.OnPc(0x00014DACu, hit("WEL_SetTimer"));       /* thunk        */
            tm.OnPc(0x00014D3Cu, hit("WEL_PostMessageW"));   /* thunk        */
            tm.OnPc(0x00014E9Cu, hit("WEL_GetMessageW"));    /* thunk        */
            tm.OnPc(0x00014DECu, hit("WEL_EndDialog"));      /* thunk        */
        });
    }
};

REGISTER_SERVICE(TraceWm5BootProgressionBreakpoints);

}  /* namespace */
