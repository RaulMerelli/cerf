#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceArithmeticExtension(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) {
    using namespace x86;

    /* Helper macro for "load Cpu.GPRs[N] into reg" via base+disp32. */
    auto load_gpr = [&](uint8_t reg, uint32_t n) {
        EmitMovRegBaseDisp32(cursor, reg, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4));
    };
    auto store_gpr = [&](uint32_t n, uint8_t reg) {
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4),
            reg);
    };

    /* MUL r/m32 (0xF7 /4) at [ESI + offset(GPRs[Rs])]. */
    auto mul_gpr_eax = [&](uint32_t n) {
        Emit8(cursor, 0xF7);
        EmitModRmReg(cursor, 2, kStateReg, 4);
        Emit32(cursor,
            static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + n * 4));
    };
    /* IMUL r/m32 (0xF7 /5) at [ESI + offset(GPRs[Rs])]. */
    auto imul_gpr_eax = [&](uint32_t n) {
        Emit8(cursor, 0xF7);
        EmitModRmReg(cursor, 2, kStateReg, 5);
        Emit32(cursor,
            static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + n * 4));
    };
    /* ADD r32, [ESI + disp32]. */
    auto add_eax_gpr = [&](uint8_t reg, uint32_t n) {
        Emit8(cursor, 0x03);
        EmitModRmReg(cursor, 2, kStateReg, reg);
        Emit32(cursor,
            static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + n * 4));
    };
    /* ADC r32, [ESI + disp32]. */
    auto adc_edx_gpr = [&](uint8_t reg, uint32_t n) {
        Emit8(cursor, 0x13);
        EmitModRmReg(cursor, 2, kStateReg, reg);
        Emit32(cursor,
            static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + n * 4));
    };

    switch (d->op1) {
    case 0:  /* MUL Rd = Rm * Rs */
        load_gpr(kEax, d->rm);
        mul_gpr_eax(d->rs);
        store_gpr(d->rd, kEax);
        if (d->s) {
            EmitTestRegReg(cursor, kEax, kEax);
            cursor = PlaceUpdateX86Flags(cursor, d, ctx, /*fAdd=*/true);
        }
        break;

    case 1:  /* MLA Rd = Rm * Rs + Rn */
        load_gpr(kEax, d->rm);
        mul_gpr_eax(d->rs);
        add_eax_gpr(kEax, d->rn);
        store_gpr(d->rd, kEax);
        if (d->s) {
            EmitTestRegReg(cursor, kEax, kEax);
            cursor = PlaceUpdateX86Flags(cursor, d, ctx, /*fAdd=*/true);
        }
        break;

    case 3:  /* MLS Rd = Ra - (Rn * Rm) ; CERF fields: Ra=rn, Rn=rm, Rm=rs */
        load_gpr(kEax, d->rm);
        mul_gpr_eax(d->rs);
        EmitMovRegReg(cursor, kEdx, kEax);
        load_gpr(kEax, d->rn);
        EmitSubReg32Reg32(cursor, kEax, kEdx);
        store_gpr(d->rd, kEax);
        break;

    case 4:  /* UMULL — Rn:Rd = Rm * Rs (unsigned) */
        load_gpr(kEax, d->rm);
        mul_gpr_eax(d->rs);
        store_gpr(d->rd, kEdx);  /* high half */
        store_gpr(d->rn, kEax);  /* low half */
        if (d->s) {
            cursor = PlaceUpdateLLX86Flags(cursor);
        }
        break;

    case 5:  /* UMLAL — Rn:Rd += Rm * Rs (unsigned) */
        load_gpr(kEax, d->rm);
        mul_gpr_eax(d->rs);
        add_eax_gpr(kEax, d->rn);
        adc_edx_gpr(kEdx, d->rd);
        store_gpr(d->rd, kEdx);
        store_gpr(d->rn, kEax);
        if (d->s) {
            cursor = PlaceUpdateLLX86Flags(cursor);
        }
        break;

    case 6:  /* SMULL — Rn:Rd = Rm * Rs (signed) */
        load_gpr(kEax, d->rm);
        imul_gpr_eax(d->rs);
        store_gpr(d->rd, kEdx);
        store_gpr(d->rn, kEax);
        if (d->s) {
            cursor = PlaceUpdateLLX86Flags(cursor);
        }
        break;

    case 7:  /* SMLAL — Rn:Rd += Rm * Rs (signed) */
        load_gpr(kEax, d->rm);
        imul_gpr_eax(d->rs);
        add_eax_gpr(kEax, d->rn);
        adc_edx_gpr(kEdx, d->rd);
        store_gpr(d->rd, kEdx);
        store_gpr(d->rn, kEax);
        if (d->s) {
            cursor = PlaceUpdateLLX86Flags(cursor);
        }
        break;

    default:
        /* UNDEFINED — emit raise. */
        EmitPush32(cursor, d->guest_address);
        EmitPush32(cursor,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->Cpu())));
        EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::RaiseUndefinedExceptionHelper));
        EmitAddRegImm32(cursor, kEsp, 8);
        break;
    }
    return cursor;
}
