#include "../../jit/mips/mips_cp0_emitter.h"

#include <cstdint>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../jit/mips/mips_block_context.h"
#include "../../jit/mips/mips_cpu_state.h"
#include "../../jit/mips/mips_decoded_insn.h"
#include "../../jit/mips/mips_gpr_emit.h"
#include "../../jit/mips/mips_jit.h"
#include "../../jit/mips/mips_place_fns.h"
#include "../../jit/x86_emit.h"
#include "../mips_processor_config.h"

namespace {

/* A DMTC0 whose 64-bit source has a high word that is not the sign-extension of
   its low word carries genuine 64-bit CP0 state, which the 32-bit CP0 model does
   not represent. __fastcall: rd in ECX. */
void __fastcall Dmtc0Width64Fatal(uint32_t rd) {
    LOG(Caution,
        "Vr5500Cp0Emitter: DMTC0 to cp0[%u] with a non-sign-extended 64-bit "
        "value - 64-bit CP0 state is not modeled\n", rd);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

class Vr5500Cp0Emitter : public MipsCp0Emitter {
public:
    using MipsCp0Emitter::MipsCp0Emitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::VR5500;
    }

    uint8_t* EmitMfc0(uint8_t* cursor, MipsDecodedInsn* d,
                      MipsBlockContext* ctx) override {
        return EmitFromCop0(cursor, d, ctx);
    }
    uint8_t* EmitDmfc0(uint8_t* cursor, MipsDecodedInsn* d,
                       MipsBlockContext* ctx) override {
        return EmitFromCop0(cursor, d, ctx);
    }
    uint8_t* EmitMtc0(uint8_t* cursor, MipsDecodedInsn* d,
                      MipsBlockContext* ctx) override {
        return EmitToCop0(cursor, d, ctx, /*is_dword=*/false);
    }
    uint8_t* EmitDmtc0(uint8_t* cursor, MipsDecodedInsn* d,
                       MipsBlockContext* ctx) override {
        return EmitToCop0(cursor, d, ctx, /*is_dword=*/true);
    }

private:
    /* gpr[rt] = sext32(cp0[rd]) (QEMU translate.c gen_mfc0_load32: ext_i32_tl),
       sel 0 only. rt == 0 discards the read (CP0 reads have no side effect). */
    uint8_t* EmitFromCop0(uint8_t* cursor, MipsDecodedInsn* d,
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
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, off);
        mips_emit::EmitStoreGprSextEax(cursor, d->rt);
        return cursor;
    }

    /* cp0[rd] = gpr[rt][31:0], sel 0 only; a read-only / unmodelled / absent rd
       routes to the loud stub. Count/Compare carry the in-core-timer side effects
       and EntryHi the ASID-change jump-cache flush, so they go through helpers. */
    uint8_t* EmitToCop0(uint8_t* cursor, MipsDecodedInsn* d,
                        MipsBlockContext* ctx, bool is_dword) {
        using namespace x86;
        if ((d->raw & 0x7u) != 0u) {
            return PlaceMipsUndefined(cursor, d, ctx);
        }
        const int32_t off = Cp0RegOffset(d->rd);
        if (off < 0 || !Cp0RegWritable(d->rd) ||
            !ctx->jit->CpuConfig()->HasCp0Reg(d->rd)) {
            return PlaceMipsUndefined(cursor, d, ctx);
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
};

}  // namespace

REGISTER_SERVICE_AS(Vr5500Cp0Emitter, MipsCp0Emitter);
