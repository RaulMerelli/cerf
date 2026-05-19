#include <cstddef>
#include <cstdint>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

namespace {

using namespace x86;

constexpr uint8_t kCpsrQBitMask = 0x08;
inline int8_t QFlagByteDisp() {
    return static_cast<int8_t>(offsetof(ArmCpuState, cpsr) + 3);
}

/* Saturation tail used by PlaceQAdd (after a host ADD/SUB sets OF):
   on no-overflow, fall through unchanged. On overflow, fix EAX to the
   appropriate saturation bound (positive 0x7FFFFFFF for adds-overflow
   or negative 0x80000000 for subtracts-overflow) and OR Q into CPSR. */
uint8_t* EmitSaturate(uint8_t* cursor, bool add) {
    uint8_t* no_saturation   = EmitJnoLabel(cursor);
    EmitMovRegImm32(cursor, kEax, 0x7FFFFFFFu);          /* positive bound */
    uint8_t* set_saturate_flag;
    if (add) {
        set_saturate_flag = EmitJncLabel(cursor);        /* CF=0 → positive bound stands */
    } else {
        set_saturate_flag = EmitJcLabel(cursor);         /* CF=1 → positive bound stands */
    }
    EmitIncReg(cursor, kEax);                            /* 0x7FFFFFFF + 1 = 0x80000000 */
    FixupLabel(set_saturate_flag, cursor);
    EmitOrByteBaseDisp8Imm8(cursor, kStateReg, QFlagByteDisp(), kCpsrQBitMask);
    FixupLabel(no_saturation, cursor);
    return cursor;
}

/* DSPMul half-word select. xy==0 keeps the low halfword in AX; xy==1
   shifts the high halfword down to AX. Either way, CWDE sign-extends
   AX into EAX for the subsequent signed IMUL. */
uint8_t* EmitDspSelect(uint8_t* cursor, int xy) {
    if (xy == 1) {
        EmitShrReg32Imm(cursor, kEax, 16);
    }
    EmitCwde(cursor);
    return cursor;
}

}  /* namespace */

uint8_t* PlaceQAdd(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext* /* ctx */) {
    using namespace x86;
    const bool add = (d->op1 & 1u) == 0u;
    const bool dbl = (d->op1 & 2u) != 0u;

    const int32_t rd_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);
    const int32_t rn_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4u);
    const int32_t rm_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rm * 4u);

    if (d->op1 != 1u) {
        /* All variants except QSUB load Rn first (QSUB recomputes
           the operand order with the Rm load below). */
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rn_disp);
    }
    if (dbl) {
        EmitAddReg32Reg32(cursor, kEax, kEax);          /* doubled = 2 * Rn */
        cursor = EmitSaturate(cursor, true);
    }
    if (add) {
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, rm_disp);
        cursor = EmitSaturate(cursor, true);
    } else {
        if (dbl) {
            /* Save doubled value (in EAX) into EBP before clobbering
               EAX with the Rm load. EBP is callee-preserved and not
               used by QAdd otherwise (no writeback). The reference
               uses EBX here; CERF has EBX pinned to ArmMmuState*. */
            EmitMovRegReg(cursor, kEbp, kEax);
        }
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rm_disp);
        if (dbl) {
            EmitSubReg32Reg32(cursor, kEax, kEbp);      /* Rm - doubled */
        } else {
            EmitSubRegBaseDisp32(cursor, kEax, kStateReg, rn_disp);  /* Rm - Rn */
        }
        cursor = EmitSaturate(cursor, false);
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
    return cursor;
}

uint8_t* PlaceDspMul(uint8_t*      cursor,
                     DecodedInsn*  d,
                     BlockContext* /* ctx */) {
    using namespace x86;

    /* DSP-mul encodings have Rd and Rn swapped relative to the QADD
       family. Mutate d in place so the rest of this fn reads the
       conventional roles. */
    const uint32_t tmp = d->rn;
    d->rn = d->rd;
    d->rd = tmp;

    const bool W      = (d->op1 == 1u);
    const bool accum  = (W && d->x == 0u) || ((d->op1 & 1u) == 0u);

    const int32_t rd_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);
    const int32_t rn_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4u);
    const int32_t rm_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rm * 4u);
    const int32_t rs_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rs * 4u);

    if (W) {
        EmitMovRegBaseDisp32(cursor, kEbp, kStateReg, rm_disp);
    } else {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rm_disp);
        cursor = EmitDspSelect(cursor, static_cast<int>(d->x));
        EmitMovRegReg(cursor, kEbp, kEax);
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rs_disp);
    cursor = EmitDspSelect(cursor, static_cast<int>(d->y));
    /* IMUL EBP — signed multiply EAX * EBP, result in EDX:EAX. */
    Emit8(cursor, 0xF7); EmitModRmReg(cursor, 3, kEbp, 5);

    if (W) {
        /* SMLAW/SMULW: take the upper 32 bits of the 48-bit product
           (Rs · Rm) >> 16 — shift EDX up 16, EAX down 16, OR to
           combine. Equivalent to the host's SAR EDX:EAX, 16 if it
           had one. */
        EmitShlReg32Imm(cursor, kEdx, 16);
        EmitShrReg32Imm(cursor, kEax, 16);
        EmitOrReg32Reg32(cursor, kEax, kEdx);
    }

    if (d->op1 == 2u) {
        /* SMLAL<x><y> — 64-bit accumulate into RdHi:RdLo. */
        EmitAddBaseDisp32Reg(cursor, kStateReg, rn_disp, kEax);
        EmitAdcBaseDisp32Reg(cursor, kStateReg, rd_disp, kEdx);
    } else {
        if (accum) {
            /* SMLAW/SMLA — 32-bit accumulate with Q-on-overflow. */
            EmitAddRegBaseDisp32(cursor, kEax, kStateReg, rn_disp);
            uint8_t* no_saturation = EmitJnoLabel(cursor);
            EmitOrByteBaseDisp8Imm8(cursor, kStateReg, QFlagByteDisp(), kCpsrQBitMask);
            FixupLabel(no_saturation, cursor);
        }
        EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
    }
    return cursor;
}
