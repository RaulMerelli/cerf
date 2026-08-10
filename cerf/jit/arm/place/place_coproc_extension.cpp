#include <cstddef>
#include <cstdint>

#include "../arm_emit_services.h"
#include "../coproc_emitter.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* MCRR/MRRC with bits[7:6:4]=001 encode VMOV between two ARM core
   registers and extension registers: cp_num=10 two single-precision
   registers, m = Vm:M, S[m]/S[m+1], m == 31 UNPREDICTABLE (DDI 0406C.c
   A8.8.344, pp. A8-946/947); cp_num=11 a doubleword register, m = M:Vm
   (A8.8.345). */

uint8_t* PlaceCoprocExtension(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    using namespace x86;

    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx);

    if (d->cp_num != 10u && d->cp_num != 11u) {
        return ctx->emit->Coproc()->EmitRegisterTransferDouble(cursor, d, ctx);
    }

    const uint32_t opc1 = (d->offset >> 4) & 0xFu;
    if ((opc1 & 0xDu) != 0x1u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t M      = (opc1 >> 1) & 1u;
    const uint32_t Vm     = d->offset & 0xFu;
    const bool     single = d->cp_num == 10u;
    const uint32_t m      = single ? ((Vm << 1) | M) : ((M << 4) | Vm);
    if (single && m == 31u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    const uint32_t Rt  = d->crd;
    const uint32_t Rt2 = d->rn;
    const bool to_arm  = d->l != 0u;

    const int32_t rt_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + Rt  * 4u);
    const int32_t rt2_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + Rt2 * 4u);
    const int32_t lo_disp = static_cast<int32_t>(
        offsetof(ArmCpuState, vfp_d) + (single ? m * 4u : m * 8u));
    const int32_t hi_disp = lo_disp + 4;

    if (to_arm) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, lo_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, rt_disp,  kEax);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, hi_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, rt2_disp, kEax);
    } else {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, lo_disp, kEax);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt2_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, hi_disp, kEax);
    }
    return cursor;
}
