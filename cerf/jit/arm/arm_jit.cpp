#include "arm_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_cpu.h"
#include "arm_mmu.h"
#include "arm_mmu_state.h"
#include "arm_neon.h"
#include "arm_neon_2reg_bitcount.h"
#include "arm_neon_2reg_bitwise_not.h"
#include "arm_neon_2reg_compare_zero.h"
#include "arm_neon_2reg_cvt_half_single.h"
#include "arm_neon_2reg_cvt_int_fp.h"
#include "arm_neon_2reg_narrow.h"
#include "arm_neon_2reg_pairwise_add_long.h"
#include "arm_neon_2reg_reciprocal.h"
#include "arm_neon_2reg_reverse.h"
#include "arm_neon_2reg_sat_abs_neg.h"
#include "arm_neon_2reg_scalar_mul.h"
#include "arm_neon_2reg_shuffle.h"
#include "arm_neon_2reg_swap.h"
#include "arm_neon_2reg_unary_arith.h"
#include "arm_neon_2regscalar.h"
#include "arm_neon_3difflen.h"
#include "arm_neon_3same_fp_abs_compare.h"
#include "arm_neon_3same_fp_arith.h"
#include "arm_neon_3same_fp_compare.h"
#include "arm_neon_3same_fp_fma.h"
#include "arm_neon_3same_fp_min_max.h"
#include "arm_neon_3same_fp_mul_acc.h"
#include "arm_neon_3same_fp_pair_add.h"
#include "arm_neon_3same_fp_pair_min_max.h"
#include "arm_neon_3same_fp_recip_step.h"
#include "arm_neon_one_reg_imm.h"
#include "arm_neon_sat.h"
#include "arm_neon_scalar_move.h"
#include "arm_neon_shift_imm.h"
#include "arm_neon_simd_3same.h"
#include "arm_neon_vext.h"
#include "arm_neon_vtbl.h"
#include "arm_vfp.h"
#include "coproc_emitter.h"

ArmJit::~ArmJit() {
    if (idle_event_) {
        CloseHandle(idle_event_);
        idle_event_ = nullptr;
    }
}

void ArmJit::OnReady() {
    cpu_       = &emu_.Get<ArmCpu>();
    cpu_state_ = cpu_->State();
    mmu_       = &emu_.Get<ArmMmu>();

    coproc_           = &emu_.Get<CoprocEmitter>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();

    neon_                        = &emu_.Get<ArmNeon>();
    simd3same_                   = &emu_.Get<ArmNeonSimd3Same>();
    neon_sat_                    = &emu_.Get<ArmNeonSat>();
    neon_shift_imm_              = &emu_.Get<ArmNeonShiftImm>();
    neon_one_reg_imm_            = &emu_.Get<ArmNeonOneRegImm>();
    neon_scalar_move_            = &emu_.Get<ArmNeonScalarMove>();
    neon_vext_                   = &emu_.Get<ArmNeonVext>();
    neon_vtbl_                   = &emu_.Get<ArmNeonVtbl>();
    neon_2reg_scalar_            = &emu_.Get<ArmNeon2RegScalar>();
    neon_3difflen_               = &emu_.Get<ArmNeon3DiffLen>();
    neon_2reg_bitcount_          = &emu_.Get<ArmNeon2RegBitcount>();
    neon_2reg_bitwise_not_       = &emu_.Get<ArmNeon2RegBitwiseNot>();
    neon_2reg_compare_zero_      = &emu_.Get<ArmNeon2RegCompareZero>();
    neon_2reg_cvt_half_single_   = &emu_.Get<ArmNeon2RegCvtHalfSingle>();
    neon_2reg_cvt_int_fp_        = &emu_.Get<ArmNeon2RegCvtIntFp>();
    neon_2reg_narrow_            = &emu_.Get<ArmNeon2RegNarrow>();
    neon_2reg_pairwise_add_long_ = &emu_.Get<ArmNeon2RegPairwiseAddLong>();
    neon_2reg_reciprocal_        = &emu_.Get<ArmNeon2RegReciprocal>();
    neon_2reg_reverse_           = &emu_.Get<ArmNeon2RegReverse>();
    neon_2reg_sat_abs_neg_       = &emu_.Get<ArmNeon2RegSatAbsNeg>();
    neon_2reg_scalar_mul_        = &emu_.Get<ArmNeon2RegScalarMul>();
    neon_2reg_shuffle_           = &emu_.Get<ArmNeon2RegShuffle>();
    neon_2reg_swap_              = &emu_.Get<ArmNeon2RegSwap>();
    neon_2reg_unary_arith_       = &emu_.Get<ArmNeon2RegUnaryArith>();
    neon_3same_fp_abs_compare_   = &emu_.Get<ArmNeon3SameFpAbsCompare>();
    neon_3same_fp_arith_         = &emu_.Get<ArmNeon3SameFpArith>();
    neon_3same_fp_compare_       = &emu_.Get<ArmNeon3SameFpCompare>();
    neon_3same_fp_fma_           = &emu_.Get<ArmNeon3SameFpFma>();
    neon_3same_fp_min_max_       = &emu_.Get<ArmNeon3SameFpMinMax>();
    neon_3same_fp_mul_acc_       = &emu_.Get<ArmNeon3SameFpMulAcc>();
    neon_3same_fp_pair_add_      = &emu_.Get<ArmNeon3SameFpPairAdd>();
    neon_3same_fp_pair_min_max_  = &emu_.Get<ArmNeon3SameFpPairMinMax>();
    neon_3same_fp_recip_step_    = &emu_.Get<ArmNeon3SameFpRecipStep>();
    vfp_                         = &emu_.Get<ArmVfp>();

    arena_.Initialize();

    const ArmMmuState* st = mmu_->State();
    if (st->code_word_top <= st->code_word_base) {
        LOG(Caution, "ArmJit: board declares no cached-DRAM extent; the JIT "
                "block index has no page window\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t page_base  = st->code_word_base >> 12;
    const uint32_t page_count = (st->code_word_top - st->code_word_base) >> 12;
    blocks_arm_.Initialize(page_base, page_count);
    blocks_thumb_.Initialize(page_base, page_count);

    idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idle_event_) {
        LOG(Caution, "ArmJit: CreateEventW(idle_event) failed gle=%lu\n",
            GetLastError());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    LOG(Jit, "ArmJit::OnReady: arena + %u-page block window at PA 0x%08X\n",
        page_count, st->code_word_base);
}
