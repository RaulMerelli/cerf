#include "mips_cp0_emitter.h"

#include <cstdint>

#include "../../core/log.h"
#include "../../cpu/mips_processor_config.h"
#include "mips_block_context.h"
#include "mips_cpu_state.h"
#include "mips_decoded_insn.h"
#include "mips_gpr_emit.h"
#include "mips_jit.h"
#include "mips_place_fns.h"
#include "../x86_emit.h"

namespace {

/* A DMTC0 whose 64-bit source has a high word that is not the sign-extension of
   its low word carries genuine 64-bit CP0 state, which the 32-bit CP0 model does
   not represent. __fastcall: rd in ECX. */
void __fastcall Dmtc0Width64Fatal(uint32_t rd) {
    LOG(Caution,
        "MipsCp0Emitter: DMTC0 to cp0[%u] with a non-sign-extended 64-bit "
        "value - 64-bit CP0 state is not modeled\n", rd);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

}  // namespace

uint8_t* MipsCp0Emitter::EmitMfc0(uint8_t* cursor, MipsDecodedInsn* d,
                                  MipsBlockContext* ctx) {
    return EmitFromCop0(cursor, d, ctx);
}

uint8_t* MipsCp0Emitter::EmitDmfc0(uint8_t* cursor, MipsDecodedInsn* d,
                                   MipsBlockContext* ctx) {
    return EmitFromCop0(cursor, d, ctx);
}

uint8_t* MipsCp0Emitter::EmitMtc0(uint8_t* cursor, MipsDecodedInsn* d,
                                  MipsBlockContext* ctx) {
    return EmitToCop0(cursor, d, ctx, /*is_dword=*/false);
}

uint8_t* MipsCp0Emitter::EmitDmtc0(uint8_t* cursor, MipsDecodedInsn* d,
                                   MipsBlockContext* ctx) {
    return EmitToCop0(cursor, d, ctx, /*is_dword=*/true);
}

/* gpr[rt] = sext32(cp0[rd]) (QEMU translate.c gen_mfc0_load32: ext_i32_tl),
   sel 0 only. rt == 0 discards the read (CP0 reads have no side effect). */
uint8_t* MipsCp0Emitter::EmitFromCop0(uint8_t* cursor, MipsDecodedInsn* d,
                                      MipsBlockContext* ctx) {
    using namespace x86;
    if ((d->raw & 0x7u) != 0u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    const int32_t off = Cp0RegOffset(d->rd);
    if (off < 0 || !ctx->jit->CpuConfig()->HasCp0Reg(d->rd)) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    if (d->rt == 0) {
        return cursor;
    }
    /* Random has no stored value; compute it on read (QEMU helper_mfc0_random). */
    if (d->rd == MipsCp0::kRandom) {
        EmitMovRegImm32(cursor, kEcx,
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
        EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::Mfc0RandomHelper));
        mips_emit::EmitStoreGprSextEax(cursor, d->rt);
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, off);
    mips_emit::EmitStoreGprSextEax(cursor, d->rt);
    return cursor;
}

/* cp0[rd] = gpr[rt][31:0], sel 0 only; a read-only / unmodelled / absent rd
   routes to the loud stub. Count/Compare carry the in-core-timer side effects
   and EntryHi the ASID-change jump-cache flush, so they go through helpers. */
uint8_t* MipsCp0Emitter::EmitToCop0(uint8_t* cursor, MipsDecodedInsn* d,
                                    MipsBlockContext* ctx, bool is_dword) {
    using namespace x86;
    if ((d->raw & 0x7u) != 0u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    const int32_t off = Cp0RegOffset(d->rd);
    if (off < 0 || !ctx->jit->CpuConfig()->HasCp0Reg(d->rd)) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    /* MTC0 to a read-only register is ignored on R4000-class silicon (VR4102 UM
       5.5.2 Random, 6.3.2 BadVAddr, 5.5.5 PRId read-only; QEMU gen_mtc0 marks
       REG01__RANDOM / REG08__BADVADDR / REG15__PRID ignored). start() writes Random. */
    if (!Cp0RegWritable(d->rd)) {
        return cursor;
    }
    /* DMTC0 moves 64 bits; gpr[rt] must be sext32(low) or it carries genuine
       64-bit CP0 state the 32-bit model does not hold. EDX=sext(low), compare
       to the stored high word; mismatch is fatal. */
    if (is_dword) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
        Emit8(cursor, 0x99);  /* cdq: EDX = sext(EAX) */
        EmitCmpRegBaseDisp32(cursor, kEdx, kStateReg, mips_emit::GprHiOff(d->rt));
        uint8_t* ok = EmitJzLabel(cursor);
        EmitMovRegImm32(cursor, kEcx, d->rd);
        EmitCall(cursor, reinterpret_cast<void*>(&Dmtc0Width64Fatal));
        FixupLabel(ok, cursor);
    }
    if (d->rd == MipsCp0::kCount || d->rd == MipsCp0::kCompare ||
        d->rd == MipsCp0::kEntryHi) {
        auto helper = d->rd == MipsCp0::kCount   ? &MipsJit::Mtc0CountHelper
                    : d->rd == MipsCp0::kCompare ? &MipsJit::Mtc0CompareHelper
                    :                              &MipsJit::Mtc0EntryHiHelper;
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rt));
        EmitMovRegImm32(cursor, kEdx,
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
        EmitCall(cursor, reinterpret_cast<void*>(helper));
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovBaseDisp32Reg(cursor, kStateReg, off, kEax);
    return cursor;
}
