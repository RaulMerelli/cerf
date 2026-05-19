#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceUpdateLLX86Flags(uint8_t* cursor) {
    using namespace x86;
    Emit8(cursor, 0x9F);  /* LAHF */
    constexpr uint8_t flag_mask = static_cast<uint8_t>(kX86FlagNf | kX86FlagZf);

    /* MOV AL, [ESI + offsetof(x86_flags)] */
    EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, x86_flags)));
    /* AND AH, flag_mask */
    Emit8(cursor, 0x80); EmitModRmReg(cursor, 3, kAh, 4); Emit8(cursor, flag_mask);
    /* AND AL, ~flag_mask */
    Emit8(cursor, 0x80); EmitModRmReg(cursor, 3, kAl, 4); Emit8(cursor, static_cast<uint8_t>(~flag_mask));
    /* OR AH, AL */
    Emit8(cursor, 0x0A); EmitModRmReg(cursor, 3, kAl, kAh);
    /* MOV [ESI + offsetof(x86_flags)], AH */
    EmitMovBaseDisp32Byte(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, x86_flags)), kAh);
    return cursor;
}
