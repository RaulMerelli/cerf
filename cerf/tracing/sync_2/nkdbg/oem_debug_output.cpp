#include "../../kernel_debug_sink.h"
#include "../../trace_manager.h"
#include "../../../core/cerf_emulator.h"
#include "../../../boards/board_context.h"
#include "../../../boards/ford_sync2/ford_sync_2_id.h"

#include <string>

namespace {

/* nk.exe OEMWriteDebugByte sub_80106360 (R0 = debug char; verified in nk.exe).
   Board-gated, NOT CRC-gated like the other nkdbg files: the Ford SYNC2 `.sec`
   loads no RomParser partitions, so the bundle CRC32 is 0 and a RegisterForBundle
   hook would silently never fire. */
class FordSync2OemDebugOutput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardId() == BoardId::FordSync2;
    }

    void OnReady() override {
        emu_.Get<TraceManager>().OnPc(
            0x80106360u, [this, line = std::string{}](const TraceContext& c) mutable {
                emu_.Get<KernelDebugSink>().EmitChar(
                    static_cast<char>(c.regs[0] & 0xFFu), line, "OAL");
            });
    }
};

}  // namespace

REGISTER_SERVICE(FordSync2OemDebugOutput);
