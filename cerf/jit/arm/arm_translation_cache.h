#pragma once

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../isa_block_space.h"
#include "../jit_code_arena.h"

class ArmMmu;

class ArmTranslationCache : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
    }

    JitCodeArena&  Arena()               { return arena_; }
    IsaBlockSpace& Space(bool thumb)     { return thumb ? blocks_thumb_ : blocks_arm_; }

    void* Lookup(bool thumb, uint32_t folded_pc);

    void Flush();
    void PendFlush() { flush_pending_ = true; }

    /* QEMU tlb_flush -> tcg_flush_jmp_cache (accel/tcg/cputlb.c:401). */
    void InvalidateVaCachesAll() {
        blocks_arm_.JumpCacheFlush();
        blocks_thumb_.JumpCacheFlush();
    }

    /* QEMU tlb_flush_page -> tb_jmp_cache_clear_page (accel/tcg/cputlb.c:157). */
    void InvalidateVaCachesPage(uint32_t folded_va) {
        blocks_arm_.JumpCacheClearPage(folded_va);
        blocks_thumb_.JumpCacheClearPage(folded_va);
    }

    void ContextSwitchFlush();
    void InvalidateDirtyCodePages();

    static void __fastcall ContextSwitchFlushHelper(ArmTranslationCache* tc);
    static void __fastcall InvalidateDirtyCodePagesHelper(ArmTranslationCache* tc);

    static void __fastcall ItlbInvalidateAllHelper(ArmTranslationCache* tc);
    static void __fastcall DtlbInvalidateAllHelper(ArmTranslationCache* tc);
    static void __fastcall UtlbInvalidateAllHelper(ArmTranslationCache* tc);
    static void __fastcall ItlbInvalidateMvaHelper(uint32_t mva, ArmTranslationCache* tc);
    static void __fastcall DtlbInvalidateMvaHelper(uint32_t mva, ArmTranslationCache* tc);
    static void __fastcall UtlbInvalidateMvaHelper(uint32_t mva, ArmTranslationCache* tc);

private:
    JitCodeArena  arena_;
    IsaBlockSpace blocks_arm_;
    IsaBlockSpace blocks_thumb_;
    bool          flush_pending_ = false;

    ArmMmu* mmu_ = nullptr;
};
