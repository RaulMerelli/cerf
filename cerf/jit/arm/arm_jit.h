#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../tracing/trace_manager.h"
#include "../guest_engine.h"
#include "../isa_block_space.h"
#include "cpu_state.h"

class ArmCpu;
class ArmMmu;
class ArmProcessorConfig;
class CoprocEmitter;
class ArmNeon;
class ArmNeon2RegBitcount;
class ArmNeon2RegBitwiseNot;
class ArmNeon2RegCompareZero;
class ArmNeon2RegCvtHalfSingle;
class ArmNeon2RegCvtIntFp;
class ArmNeon2RegNarrow;
class ArmNeon2RegPairwiseAddLong;
class ArmNeon2RegReciprocal;
class ArmNeon2RegReverse;
class ArmNeon2RegSatAbsNeg;
class ArmNeon2RegScalar;
class ArmNeon2RegScalarMul;
class ArmNeon2RegShuffle;
class ArmNeon2RegSwap;
class ArmNeon2RegUnaryArith;
class ArmNeon3DiffLen;
class ArmNeon3SameFpAbsCompare;
class ArmNeon3SameFpArith;
class ArmNeon3SameFpCompare;
class ArmNeon3SameFpFma;
class ArmNeon3SameFpMinMax;
class ArmNeon3SameFpMulAcc;
class ArmNeon3SameFpPairAdd;
class ArmNeon3SameFpPairMinMax;
class ArmNeon3SameFpRecipStep;
class ArmNeonOneRegImm;
class ArmNeonSat;
class ArmNeonScalarMove;
class ArmNeonShiftImm;
class ArmNeonSimd3Same;
class ArmNeonVext;
class ArmNeonVtbl;
class ArmVfp;

class ArmJit : public GuestEngine {
public:
    using GuestEngine::GuestEngine;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
    }

    ArmCpuState* CpuState() { return cpu_state_; }

    ArmCpu*             Cpu()             { return cpu_; }
    ArmMmu*             Mmu()             { return mmu_; }
    CoprocEmitter*      Coproc()          { return coproc_; }
    ArmProcessorConfig* ProcessorConfig() { return processor_config_; }

    ArmNeon*                    Neon()                   { return neon_; }
    ArmNeonSimd3Same*           Simd3Same()              { return simd3same_; }
    ArmNeonSat*                 NeonSat()                { return neon_sat_; }
    ArmNeonShiftImm*            NeonShiftImm()           { return neon_shift_imm_; }
    ArmNeonOneRegImm*           NeonOneRegImm()          { return neon_one_reg_imm_; }
    ArmNeonScalarMove*          NeonScalarMove()         { return neon_scalar_move_; }
    ArmNeonVext*                NeonVext()               { return neon_vext_; }
    ArmNeonVtbl*                NeonVtbl()               { return neon_vtbl_; }
    ArmNeon2RegScalar*          Neon2RegScalar()         { return neon_2reg_scalar_; }
    ArmNeon3DiffLen*            Neon3DiffLen()           { return neon_3difflen_; }
    ArmNeon2RegBitcount*        Neon2RegBitcount()       { return neon_2reg_bitcount_; }
    ArmNeon2RegBitwiseNot*      Neon2RegBitwiseNot()     { return neon_2reg_bitwise_not_; }
    ArmNeon2RegCompareZero*     Neon2RegCompareZero()    { return neon_2reg_compare_zero_; }
    ArmNeon2RegCvtHalfSingle*   Neon2RegCvtHalfSingle()  { return neon_2reg_cvt_half_single_; }
    ArmNeon2RegCvtIntFp*        Neon2RegCvtIntFp()       { return neon_2reg_cvt_int_fp_; }
    ArmNeon2RegNarrow*          Neon2RegNarrow()         { return neon_2reg_narrow_; }
    ArmNeon2RegPairwiseAddLong* Neon2RegPairwiseAddLong() { return neon_2reg_pairwise_add_long_; }
    ArmNeon2RegReciprocal*      Neon2RegReciprocal()     { return neon_2reg_reciprocal_; }
    ArmNeon2RegReverse*         Neon2RegReverse()        { return neon_2reg_reverse_; }
    ArmNeon2RegSatAbsNeg*       Neon2RegSatAbsNeg()      { return neon_2reg_sat_abs_neg_; }
    ArmNeon2RegScalarMul*       Neon2RegScalarMul()      { return neon_2reg_scalar_mul_; }
    ArmNeon2RegShuffle*         Neon2RegShuffle()        { return neon_2reg_shuffle_; }
    ArmNeon2RegSwap*            Neon2RegSwap()           { return neon_2reg_swap_; }
    ArmNeon2RegUnaryArith*      Neon2RegUnaryArith()     { return neon_2reg_unary_arith_; }
    ArmNeon3SameFpAbsCompare*   Neon3SameFpAbsCompare()  { return neon_3same_fp_abs_compare_; }
    ArmNeon3SameFpArith*        Neon3SameFpArith()       { return neon_3same_fp_arith_; }
    ArmNeon3SameFpCompare*      Neon3SameFpCompare()     { return neon_3same_fp_compare_; }
    ArmNeon3SameFpFma*          Neon3SameFpFma()         { return neon_3same_fp_fma_; }
    ArmNeon3SameFpMinMax*       Neon3SameFpMinMax()      { return neon_3same_fp_min_max_; }
    ArmNeon3SameFpMulAcc*       Neon3SameFpMulAcc()      { return neon_3same_fp_mul_acc_; }
    ArmNeon3SameFpPairAdd*      Neon3SameFpPairAdd()     { return neon_3same_fp_pair_add_; }
    ArmNeon3SameFpPairMinMax*   Neon3SameFpPairMinMax()  { return neon_3same_fp_pair_min_max_; }
    ArmNeon3SameFpRecipStep*    Neon3SameFpRecipStep()   { return neon_3same_fp_recip_step_; }
    ArmVfp*                     Vfp()                    { return vfp_; }

    void SetInterruptPending();
    void ClearInterruptPending();

    void ContextSwitchFlush();

    void InvalidateDirtyCodePages();

    /* QEMU tlb_flush -> tcg_flush_jmp_cache (accel/tcg/cputlb.c:401). */
    void InvalidateVaCachesAll() {
        blocks_arm_.JumpCacheFlush();
        blocks_thumb_.JumpCacheFlush();
        shadow_stack_count_ = 0;
    }

    /* QEMU tlb_flush_page -> tb_jmp_cache_clear_page (accel/tcg/cputlb.c:157). */
    void InvalidateVaCachesPage(uint32_t folded_va) {
        blocks_arm_.JumpCacheClearPage(folded_va);
        blocks_thumb_.JumpCacheClearPage(folded_va);
        shadow_stack_count_ = 0;
    }

    static void __fastcall WfiHelper(ArmJit* jit);

    static void __fastcall ItlbInvalidateAllHelper(ArmJit* jit);
    static void __fastcall DtlbInvalidateAllHelper(ArmJit* jit);
    static void __fastcall UtlbInvalidateAllHelper(ArmJit* jit);
    static void __fastcall ItlbInvalidateMvaHelper(uint32_t mva, ArmJit* jit);
    static void __fastcall DtlbInvalidateMvaHelper(uint32_t mva, ArmJit* jit);
    static void __fastcall UtlbInvalidateMvaHelper(uint32_t mva, ArmJit* jit);

    static void __fastcall EnterDeepSleepHelper(ArmJit* jit);

    static void __fastcall ContextSwitchFlushHelper(ArmJit* jit);

    static void __fastcall InvalidateDirtyCodePagesHelper(ArmJit* jit);

    static void __fastcall SrsHelper(uint32_t encoded, ArmJit* jit,
                                     uint32_t guest_pc);

    static uint32_t __fastcall RfeHelper(uint32_t rn_value, uint32_t encoded,
                                         ArmJit* jit);

    static uint8_t* __fastcall TranslateReadHelper(uint32_t va, ArmJit* jit);
    static uint8_t* __fastcall TranslateWriteHelper(uint32_t va, ArmJit* jit);
    static uint8_t* __fastcall TranslateReadWriteHelper(uint32_t va, ArmJit* jit);

    static void* __fastcall RaiseIrqExceptionHelper(uint32_t target_pc,
                                                    ArmJit*  jit);

    void     Run() override;
    bool     DeepSleep()    const override { return cpu_state_->deep_sleep != 0; }
    bool     ResetPending() const override { return cpu_state_->reset_pending != 0; }
    uint32_t Pc()           const override { return cpu_state_->gprs[ArmGpr::kR15]; }
    void     DispatchTraceIter() override {
#if CERF_DEV_MODE
        emu_.Get<TraceManager>().DispatchRunLoopIter(cpu_state_->gprs,
                                                     cpu_state_->cpsr.word);
#endif
    }

    std::optional<uint8_t*> PeekGuestVa(uint32_t va) override;
    uint8_t* ResolveGuestVaToHost(uint32_t va) override;
    bool     ResolveGuestVaToPa(uint32_t va, uint32_t* pa) override;

    void SaveCpuState(StateWriter& w)    override;
    void RestoreCpuState(StateReader& r) override;
    void SaveMmuState(StateWriter& w)    override;
    void RestoreMmuState(StateReader& r) override;

    void ResyncInterruptPoll() override;
    void FlushTranslationCache(uint32_t va, uint32_t length) override;
    void SetResetPending(bool is_resume = false) override;
    void EnterDeepSleep() override;
    void ExitDeepSleep() override;
    void SetInjectionBand(uint32_t va, uint32_t pa, uint32_t size) override;
    void SetDmaRegion(uint32_t pa, uint32_t size) override;

private:
    void UpdateInterruptOnPoll();

    ArmCpu*             cpu_              = nullptr;
    ArmMmu*             mmu_              = nullptr;
    ArmCpuState*        cpu_state_        = nullptr;
    CoprocEmitter*      coproc_           = nullptr;
    ArmProcessorConfig* processor_config_ = nullptr;

    ArmNeon*                    neon_                       = nullptr;
    ArmNeonSimd3Same*           simd3same_                  = nullptr;
    ArmNeonSat*                 neon_sat_                   = nullptr;
    ArmNeonShiftImm*            neon_shift_imm_             = nullptr;
    ArmNeonOneRegImm*           neon_one_reg_imm_           = nullptr;
    ArmNeonScalarMove*          neon_scalar_move_           = nullptr;
    ArmNeonVext*                neon_vext_                  = nullptr;
    ArmNeonVtbl*                neon_vtbl_                  = nullptr;
    ArmNeon2RegScalar*          neon_2reg_scalar_           = nullptr;
    ArmNeon3DiffLen*            neon_3difflen_              = nullptr;
    ArmNeon2RegBitcount*        neon_2reg_bitcount_         = nullptr;
    ArmNeon2RegBitwiseNot*      neon_2reg_bitwise_not_      = nullptr;
    ArmNeon2RegCompareZero*     neon_2reg_compare_zero_     = nullptr;
    ArmNeon2RegCvtHalfSingle*   neon_2reg_cvt_half_single_  = nullptr;
    ArmNeon2RegCvtIntFp*        neon_2reg_cvt_int_fp_       = nullptr;
    ArmNeon2RegNarrow*          neon_2reg_narrow_           = nullptr;
    ArmNeon2RegPairwiseAddLong* neon_2reg_pairwise_add_long_ = nullptr;
    ArmNeon2RegReciprocal*      neon_2reg_reciprocal_       = nullptr;
    ArmNeon2RegReverse*         neon_2reg_reverse_          = nullptr;
    ArmNeon2RegSatAbsNeg*       neon_2reg_sat_abs_neg_      = nullptr;
    ArmNeon2RegScalarMul*       neon_2reg_scalar_mul_       = nullptr;
    ArmNeon2RegShuffle*         neon_2reg_shuffle_          = nullptr;
    ArmNeon2RegSwap*            neon_2reg_swap_             = nullptr;
    ArmNeon2RegUnaryArith*      neon_2reg_unary_arith_      = nullptr;
    ArmNeon3SameFpAbsCompare*   neon_3same_fp_abs_compare_  = nullptr;
    ArmNeon3SameFpArith*        neon_3same_fp_arith_        = nullptr;
    ArmNeon3SameFpCompare*      neon_3same_fp_compare_      = nullptr;
    ArmNeon3SameFpFma*          neon_3same_fp_fma_          = nullptr;
    ArmNeon3SameFpMinMax*       neon_3same_fp_min_max_      = nullptr;
    ArmNeon3SameFpMulAcc*       neon_3same_fp_mul_acc_      = nullptr;
    ArmNeon3SameFpPairAdd*      neon_3same_fp_pair_add_     = nullptr;
    ArmNeon3SameFpPairMinMax*   neon_3same_fp_pair_min_max_ = nullptr;
    ArmNeon3SameFpRecipStep*    neon_3same_fp_recip_step_   = nullptr;
    ArmVfp*                     vfp_                        = nullptr;

    IsaBlockSpace blocks_arm_;
    IsaBlockSpace blocks_thumb_;

    uint32_t   shadow_stack_count_ = 0;
    std::mutex interrupt_lock_;
};
