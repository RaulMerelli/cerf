#include <cstddef>
#include <cstdint>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

namespace {

using namespace x86;

/* Emit AND DWORD PTR [ESI + offsetof(cpsr)], ~0x20 — clears CPSR
   bit 5 (ThumbMode). The reference uses an absolute-disp32 form
   against &Cpu.CPSR; CERF uses [base+disp32] off the pinned ESI
   so the emit is per-instance-state-aware. */
inline void EmitClearCpsrThumbBit(uint8_t*& cursor) {
    Emit8(cursor, 0x81);
    EmitModRmReg(cursor, 2, kStateReg, 4);
    Emit32(cursor, static_cast<uint32_t>(offsetof(ArmCpuState, cpsr)));
    Emit32(cursor, ~0x20u);
}

constexpr int32_t GprDisp(uint32_t reg_num) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + reg_num * 4u);
}

}  /* namespace */

uint8_t* PlaceThumbBranchAndExchange(uint8_t*      cursor,
                                     DecodedInsn*  d,
                                     BlockContext* ctx) {
    using namespace x86;

    /* Thumb BX encodes its target register as RsHs + 8*H2 (the 3-bit
       Rs/Hs field plus the high-bit selector H2). */
    const uint32_t rd = d->rs_hs + 8u * d->h2;

    if (rd == ArmGpr::kR15) {
        /* BX R15 always switches to ARM mode (regardless of the LSB,
           since R15's value at this point is the prefetch-advanced PC
           which has no encoded mode bit). */
        EmitMovRegImm32(cursor, kEax, d->guest_address + 4u);
        EmitClearCpsrThumbBit(cursor);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR15), kEax);
    } else {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(rd));
        EmitTestRegImm32(cursor, kEax, 1u);
        uint8_t* remain_in_thumb = EmitJnzLabel(cursor);

        /* ARM-mode branch: clear ThumbMode + commit R15. */
        EmitClearCpsrThumbBit(cursor);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR15), kEax);
        cursor = PlaceR15ModifiedHelper(cursor, d, ctx);

        FixupLabel(remain_in_thumb, cursor);
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR15), kEax);
    }
    cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
    return cursor;
}

uint8_t* PlaceThumbLoadAddressPC(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* /* ctx */) {
    using namespace x86;
    /* ADD Rd, PC, #imm — the EA is fully known at emit time:
       (PC + 4) word-aligned, plus the 8-bit imm shifted left 2. */
    const uint32_t address =
        ((d->guest_address + 4u) & 0xFFFFFFFCu) +
        (static_cast<uint32_t>(d->word8) << 2);
    EmitMovRegImm32(cursor, kEax, address);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

uint8_t* PlaceThumbLongBranch(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    using namespace x86;

    switch (d->h_two_bits) {
    case 0:
        /* Unconditional B (Thumb long branch, single instruction). */
        EmitMovRegImm32(cursor, kEax,
            d->guest_address + 4u + static_cast<uint32_t>(d->offset));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR15), kEax);
        cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
        break;

    case 2:
        /* BL high half — stage the upper-half destination in R14;
           the next instruction (low half, case 1 or 3) commits R15
           and pushes the return on the shadow stack. */
        EmitMovRegImm32(cursor, kEax,
            d->guest_address + 4u + static_cast<uint32_t>(d->offset));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR14), kEax);
        break;

    case 1:
        /* BLX low half — Thumb→ARM mode switch + branch. Clear
           ThumbMode, R15 = (R14 + offset) & ~3 (word-align for ARM),
           R14 = PC + 3 (one opcode ahead + Thumb-bit indicator),
           push shadow stack, R15Modified. */
        EmitClearCpsrThumbBit(cursor);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(ArmGpr::kR14));
        EmitAddRegImm32(cursor, kEax, static_cast<uint32_t>(d->offset));
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFCu);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR15), kEax);
        EmitMovRegImm32(cursor, kEax, d->guest_address + 3u);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR14), kEax);
        cursor = PlacePushShadowStack(cursor, d, ctx);
        cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
        break;

    case 3:
        /* BL low half — Thumb→Thumb branch. R15 = R14 + offset
           (no alignment mask; Thumb instructions are halfword-aligned
           already), R14 = PC + 3, push shadow stack, R15Modified. */
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(ArmGpr::kR14));
        EmitAddRegImm32(cursor, kEax, static_cast<uint32_t>(d->offset));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR15), kEax);
        EmitMovRegImm32(cursor, kEax, d->guest_address + 3u);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR14), kEax);
        cursor = PlacePushShadowStack(cursor, d, ctx);
        cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
        break;

    default:
        /* h_two_bits is 2 bits wide so always 0..3 — unreachable. */
        break;
    }
    return cursor;
}
