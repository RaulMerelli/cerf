#include "../../jit/arm/coproc_emitter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>

#include "../../core/cerf_emulator.h"
#include "../../jit/arm/arm_cpu.h"
#include "../../jit/arm/arm_emit_services.h"
#include "../../jit/arm/arm_interrupt_channel.h"
#include "../../jit/arm/arm_mmu_state.h"
#include "../../jit/arm/cpu_state.h"
#include "../../jit/arm/place_fns.h"
#include "../../jit/x86_emit_alu.h"
#include "../../boards/board_context.h"
#include "../../socs/iop13xx/iop13xx_cp6.h"

namespace {

class XscaleCoprocEmitterBase : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    uint8_t* EmitRegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) override {
        if (uint8_t* soc_cursor = EmitSocRegisterTransfer(cursor, d, ctx)) {
            return soc_cursor;
        }
        if (d->cp_num == 15) {
            /* CPAR (Coprocessor Access Register) - XScale §7.2.15: cp15
               c15, CRm=c1, opc2=0. The shared cp15 dispatch fatals on
               c15, so handle here. Backed by ArmMmuState::
               coprocessor_access, unused on XScale unless HasCp15V6(). */
            if (d->crn == 15 && d->crm == 1 && d->cp == 0 && d->cp_opc == 0) {
                using namespace x86;
                const int32_t rd_disp = static_cast<int32_t>(
                    offsetof(ArmCpuState, gprs) + d->rd * 4u);
                const int32_t cpar_disp = static_cast<int32_t>(
                    offsetof(ArmMmuState, coprocessor_access));
                if (d->l) {
                    EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, cpar_disp);
                    EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
                } else {
                    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                    EmitMovBaseDisp32Reg(cursor, kMmuReg, cpar_disp, kEax);
                }
                return cursor;
            }
            /* CP15 c14 = XScale debug/breakpoint regs (Table 7-19); no
               breakpoints modeled → read 0, ignore writes. Delegating to the
               shared cp15 body UNDs CRn=14, and the OAL suspend state-save reads
               these - the UND becomes a fatal exception-storm halt. */
            if (d->crn == 14) {
                const bool is_dbg = d->cp_opc == 0 && d->cp == 0 &&
                    (d->crm == 0 || d->crm == 3 || d->crm == 4 ||
                     d->crm == 8 || d->crm == 9);
                if (!is_dbg) return EmitCoprocUnimplementedFatal(cursor, d, ctx);
                if (d->l) {
                    using namespace x86;
                    const int32_t rd_disp = static_cast<int32_t>(
                        offsetof(ArmCpuState, gprs) + d->rd * 4u);
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp, 0u);
                }
                return cursor;
            }
            /* Allocate Data Cache Line (c7, c2, opc2=5) - XScale-specific
               (Core Dev Manual Table 7-12). No D-cache is modeled and the
               line's backing memory is real, so emit nothing. */
            if (d->crn == 7 && d->crm == 2 && d->cp == 5 && d->cp_opc == 0) {
                return cursor;
            }
            /* XScale Core Dev Manual Table 7-3 (page 80): Auxiliary Control
               is c1 / Opc_1=0 / CRm=0 / Opc_2=1 - the shared body's ACTLR
               path. */
            return EmitCp15RegisterTransfer(cursor, d, ctx);
        }
        if (d->cp_num == 14 && d->cp_opc == 0 && d->crm == 0 && d->cp == 0) {
            using namespace x86;
            const int32_t rd_disp = static_cast<int32_t>(
                offsetof(ArmCpuState, gprs) + d->rd * 4u);

            /* PWRMODE (CRn=c7) - XScale Core Dev Manual Table 7-23, M=bits[3:0].
               Read returns 0 (ACTIVE). On write: M=1 IDLE (OEMIdle sub_800F736C)
               halts till the next IRQ; M=3 SLEEP (OEMPowerOff sub_800F73B0) is a
               real power-down - notify the user, then park. */
            if (d->crn == 7) {
                if (d->l) {
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp, 0u);
                    return cursor;
                }
                EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
                EmitAndRegImm32(cursor, kEax, 0xFu);
                EmitCmpRegImm32(cursor, kEax, 3u);
                uint8_t* not_sleep = EmitJnzLabel(cursor);
                /* M=3 SLEEP: halt the CPU + recovery prompt; the halt replaces
                   the idle wait, so skip the WfiHelper fall-through. */
                EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
                EmitCall(cursor,
                    reinterpret_cast<void*>(&ArmCpu::EnterDeepSleepHelper));
                uint8_t* done = EmitJmpLabel(cursor);
                FixupLabel(not_sleep, cursor);
                /* M=1 IDLE: wait for the next interrupt. */
                EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                        ctx->emit->InterruptChannel())));
                EmitCall(cursor,
                    reinterpret_cast<void*>(&ArmInterruptChannel::WfiHelper));
                FixupLabel(done, cursor);
                return cursor;
            }

            /* CCLKCFG (CRn=c6) - XScale Core Dev Manual Table 7-25. The
               frequency change completes instantly under emulation (no PLL
               relock), so a write has no retained state; reads return 0
               (active, non-turbo). */
            if (d->crn == 6) {
                if (d->l) {
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp, 0u);
                }
                return cursor;
            }
            /* CP14 c0-c5 perfmon + c8-c15 debug (XScale Core Dev Manual §8,
               §7.1): the perfmon counters and JTAG debug are not modeled, so
               these read 0 (inactive) and ignore writes. The OAL suspend
               state-save reads the whole CP14 bank, so a FATAL/UND here halts. */
            if (d->l) EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp, 0u);
            return cursor;
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataTransfer(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) override {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataOperation(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) override {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* MCRR/MRRC - XScale supports these only to CP0, the DSP 40-bit
       accumulator acc0 (MAR/MRA, Core Dev Manual §2.3.1 Table 2-6); any
       other coprocessor UNDs (§2.2.4). */
    uint8_t* EmitRegisterTransferDouble(uint8_t*      cursor,
                                        DecodedInsn*  d,
                                        BlockContext* ctx) override {
        using namespace x86;
        if (d->cp_num != 0u) {
            return EmitRaiseUndAndReturn(cursor, d, ctx);
        }

        const uint32_t rdlo = d->crd;       /* RdLo (bits[15:12]) */
        const uint32_t rdhi = d->rn;        /* RdHi (bits[19:16]) */
        const bool to_arm   = d->l != 0u;   /* L=1 → MRRC = MRA (read acc0) */

        /* XScale Core Dev Manual §7.2.15 (page 94): an access to CP0 with
           CPAR bit 0 clear raises an undefined exception; the OS grants the
           bit and the access is retried. */
        const int32_t cpar_disp = static_cast<int32_t>(
            offsetof(ArmMmuState, coprocessor_access));
        EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, cpar_disp);
        EmitAndRegImm32(cursor, kEax, 1u);
        uint8_t* enabled = EmitJnzLabel(cursor);
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        FixupLabel(enabled, cursor);

        const int32_t rdlo_disp = static_cast<int32_t>(
            offsetof(ArmCpuState, gprs) + rdlo * 4u);
        const int32_t rdhi_disp = static_cast<int32_t>(
            offsetof(ArmCpuState, gprs) + rdhi * 4u);
        const int32_t acc_lo_disp =
            static_cast<int32_t>(offsetof(ArmCpuState, acc0));
        const int32_t acc_hi_disp = acc_lo_disp + 4;

        if (to_arm) {
            /* MRA: RdLo = acc0[31:0]; RdHi = sign_extend(acc0[39:32]). */
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, acc_lo_disp);
            EmitMovBaseDisp32Reg(cursor, kStateReg, rdlo_disp, kEax);
            EmitMovsxByteRegBaseDisp32(cursor, kEax, kStateReg, acc_hi_disp);
            EmitMovBaseDisp32Reg(cursor, kStateReg, rdhi_disp, kEax);
        } else {
            /* MAR: acc0[31:0] = RdLo; acc0[39:32] = RdHi[7:0]. */
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rdlo_disp);
            EmitMovBaseDisp32Reg(cursor, kStateReg, acc_lo_disp, kEax);
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rdhi_disp);
            EmitMovBaseDisp32Byte(cursor, kStateReg, acc_hi_disp, kAl);
        }
        return cursor;
    }

protected:
    virtual uint8_t* EmitSocRegisterTransfer(uint8_t*, DecodedInsn*,
                                              BlockContext*) {
        return nullptr;
    }
};

class Pxa25xXscaleCoprocEmitter : public XscaleCoprocEmitterBase {
public:
    using XscaleCoprocEmitterBase::XscaleCoprocEmitterBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::PXA25x;
    }
};

class Iop13xxXscaleCoprocEmitter : public XscaleCoprocEmitterBase {
public:
    using XscaleCoprocEmitterBase::XscaleCoprocEmitterBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::IOP13xx;
    }

protected:
    uint8_t* EmitSocRegisterTransfer(uint8_t* cursor, DecodedInsn* d,
                                      BlockContext* ctx) override {
        /* Third Generation Intel XScale Microarchitecture Developer's Manual,
           Table 44: modeled caches need no host operation for these writes. */
        if (d->cp_num == 15 && !d->l && d->crn == 7 && d->cp_opc == 1) {
            const bool invalidate_mva = d->crm == 7 && d->cp == 1;
            const bool clean = d->crm == 11 && (d->cp == 1 || d->cp == 2);
            const bool clean_invalidate = d->crm == 15 && d->cp == 2;
            if (invalidate_mva || clean || clean_invalidate) return cursor;
        }
        if (d->cp_num != 6) return nullptr;
        return static_cast<Iop13xxCp6&>(emu_.Get<IrqController>())
            .EmitRegisterTransfer(cursor, d, ctx);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Pxa25xXscaleCoprocEmitter, CoprocEmitter);
REGISTER_SERVICE_AS(Iop13xxXscaleCoprocEmitter, CoprocEmitter);
