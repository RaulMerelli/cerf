#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

uint8_t* EmitArmInterworkingMaskEax(uint8_t* cursor) {
    using namespace x86;

    /* TEST EAX, 1 - bit 0 selects new ISA state. */
    EmitTestRegImm32(cursor, kEax, 1);
    uint8_t* jz_to_rejoin = EmitJzLabel(cursor);

    /* OR DWORD PTR [ESI + cpsr], 0x20 - set CPSR.T (bit 5) for Thumb target.
       81 /1 mod=10 r/m=ESI disp32 imm32. */
    Emit8(cursor, 0x81);
    EmitModRmReg(cursor, 2, kStateReg, 1);
    Emit32(cursor, static_cast<uint32_t>(offsetof(ArmCpuState, cpsr)));
    Emit32(cursor, 0x00000020u);
    /* AND EAX, 0xFFFFFFFE - drop interworking bit. */
    EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);

    FixupLabel(jz_to_rejoin, cursor);
    return cursor;
}

uint8_t* EmitArmInterworkingFullEax(uint8_t* cursor) {
    using namespace x86;

    /* Bidirectional interworking write (DDI0406C §A2.3.1): T is also
       CLEARED on a bit0==0 target - a Thumb-state POP/LDM to an ARM
       address must leave Thumb state, which the set-only
       EmitArmInterworkingMaskEax cannot do. */
    EmitTestRegImm32(cursor, kEax, 1);
    uint8_t* jz_to_arm = EmitJzLabel(cursor);

    /* OR DWORD PTR [ESI + cpsr], 0x20 - set CPSR.T. */
    Emit8(cursor, 0x81);
    EmitModRmReg(cursor, 2, kStateReg, 1);
    Emit32(cursor, static_cast<uint32_t>(offsetof(ArmCpuState, cpsr)));
    Emit32(cursor, 0x00000020u);
    EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);
    uint8_t* jmp_done = EmitJmpLabel(cursor);

    FixupLabel(jz_to_arm, cursor);
    /* AND DWORD PTR [ESI + cpsr], ~0x20 - clear CPSR.T. */
    Emit8(cursor, 0x81);
    EmitModRmReg(cursor, 2, kStateReg, 4);
    Emit32(cursor, static_cast<uint32_t>(offsetof(ArmCpuState, cpsr)));
    Emit32(cursor, ~0x20u);
    EmitAndRegImm32(cursor, kEax, 0xFFFFFFFCu);

    FixupLabel(jmp_done, cursor);
    return cursor;
}
