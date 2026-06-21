#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

namespace {

/* Compute the offset into ArmCpuState::gprs[] for register index N. */
constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

}  /* namespace */

/* BFI: Rd[lsb+width-1:lsb] = Rn[width-1:0]; other Rd bits unchanged. */
uint8_t* PlaceBfi(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext* /*ctx*/) {
    using namespace x86;
    const uint32_t lsb       = d->op1;
    const uint32_t width     = d->rs;
    const uint32_t src_mask  = (width == 32u) ? 0xFFFFFFFFu : ((1u << width) - 1u);

    /* MOV EAX, [ESI + GPRs[Rn]]              ; EAX = source value */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
    /* AND EAX, src_mask                       ; keep low `width` bits */
    EmitAndRegImm32(cursor, kEax, src_mask);
    /* SHL EAX, lsb                            ; align into target field */
    if (lsb != 0u) {
        Emit8(cursor, 0xC1);
        EmitModRmReg(cursor, /*mod=*/3, /*rm=*/kEax, /*reg=*/4);
        Emit8(cursor, static_cast<uint8_t>(lsb));
    }
    /* MOV ECX, [ESI + GPRs[Rd]]               ; ECX = current Rd */
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rd));
    /* AND ECX, ~mask                          ; clear target field */
    EmitAndRegImm32(cursor, kEcx, ~d->immediate);
    /* OR  ECX, EAX                            ; insert prepared value */
    EmitOrReg32Reg32(cursor, kEcx, kEax);
    /* MOV [ESI + GPRs[Rd]], ECX */
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEcx);
    return cursor;
}

/* BFC: Rd[lsb+width-1:lsb] = 0; other Rd bits unchanged. */
uint8_t* PlaceBfc(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext* /*ctx*/) {
    using namespace x86;
    /* MOV EAX, [ESI + GPRs[Rd]] ; AND EAX, ~mask ; MOV back. */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rd));
    EmitAndRegImm32     (cursor, kEax, ~d->immediate);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

/* UBFX: Rd = ZeroExtend(Rn[lsb+width-1:lsb]). */
uint8_t* PlaceUbfx(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext* /*ctx*/) {
    using namespace x86;
    const uint32_t lsb      = d->op1;
    const uint32_t width    = d->rs;
    const uint32_t mask     = (width == 32u) ? 0xFFFFFFFFu : ((1u << width) - 1u);

    /* MOV EAX, [ESI + GPRs[Rn]] */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
    /* SHR EAX, lsb */
    if (lsb != 0u) {
        Emit8(cursor, 0xC1);
        EmitModRmReg(cursor, /*mod=*/3, /*rm=*/kEax, /*reg=*/5);
        Emit8(cursor, static_cast<uint8_t>(lsb));
    }
    /* AND EAX, mask */
    EmitAndRegImm32(cursor, kEax, mask);
    /* MOV [ESI + GPRs[Rd]], EAX */
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

/* SBFX: Rd = SignExtend(Rn[lsb+width-1:lsb]). x86 sign-extend trick -
   shift the field to the top, then arithmetic-shift right by
   (32 - width). */
uint8_t* PlaceSbfx(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext* /*ctx*/) {
    using namespace x86;
    const uint32_t lsb        = d->op1;
    const uint32_t width      = d->rs;
    const uint32_t lshift     = 32u - lsb - width;
    const uint32_t rshift     = 32u - width;

    /* MOV EAX, [ESI + GPRs[Rn]] */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
    /* SHL EAX, lshift          ; field is now [31 : 32-width] */
    if (lshift != 0u) {
        Emit8(cursor, 0xC1);
        EmitModRmReg(cursor, /*mod=*/3, /*rm=*/kEax, /*reg=*/4);
        Emit8(cursor, static_cast<uint8_t>(lshift));
    }
    /* SAR EAX, rshift           ; arithmetic shift right, sign-extends */
    if (rshift != 0u) {
        Emit8(cursor, 0xC1);
        EmitModRmReg(cursor, /*mod=*/3, /*rm=*/kEax, /*reg=*/7);
        Emit8(cursor, static_cast<uint8_t>(rshift));
    }
    /* MOV [ESI + GPRs[Rd]], EAX */
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}
