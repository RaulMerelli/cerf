#include <cstddef>

#include "../../../cpu/arm_processor_config.h"
#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* LoadWritePC (DDI 0406C.c A2.3.2, p. A2-47): BXWritePC from ARMv5 on,
   else BranchWritePC. In: the loaded value in EAX. */
uint8_t* EmitLoadedPcWrite(uint8_t* cursor, DecodedInsn* d,
                           BlockContext* ctx) {
    using namespace x86;
    if (ctx->jit->ProcessorConfig()->HasLoadToPcInterworking()) {
        cursor = EmitArmInterworkingFullEax(cursor);
    } else {
        /* BranchWritePC (A2.3.2, p. A2-47): <31:2>:'00' in ARM state,
           <31:1>:'0' in Thumb state. */
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, cpsr)));
        EmitTestRegImm32(cursor, kEcx, 0x20u);
        uint8_t* thumb_l = EmitJnzLabel32(cursor);
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFCu);
        uint8_t* mask_done = EmitJmpLabel32(cursor);
        FixupLabel32(thumb_l, cursor);
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);
        FixupLabel32(mask_done, cursor);
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15u * 4u), kEax);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}
