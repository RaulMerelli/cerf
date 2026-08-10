#pragma once

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"

struct MipsCpuState;

class MipsMmu;
class MipsProcessorConfig;
class MipsTranslationCache;

class MipsCp0Ops : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    /* QEMU target/mips cp0_timer.c. */
    void TimerPoll();

    static void __fastcall TlbwiHelper(MipsCp0Ops* ops);
    static void __fastcall TlbwrHelper(MipsCp0Ops* ops);
    static void __fastcall TlbpHelper(MipsCp0Ops* ops);
    static void __fastcall TlbrHelper(MipsCp0Ops* ops);

    static uint32_t __fastcall Mfc0RandomHelper(MipsCp0Ops* ops);

    /* QEMU target/mips cp0_timer.c store_count / store_compare. */
    static void __fastcall Mtc0CountHelper(uint32_t value, MipsCp0Ops* ops);
    static void __fastcall Mtc0CompareHelper(uint32_t value, MipsCp0Ops* ops);

    /* QEMU target/mips helper_mtc0_entryhi -> tlb_flush (cp0_helper.c:1142). */
    static void __fastcall Mtc0EntryHiHelper(uint32_t value, MipsCp0Ops* ops);

    static void __fastcall EretHelper(MipsCp0Ops* ops);
    static void __fastcall RfeHelper(MipsCp0Ops* ops);

    MipsCpuState*         CpuState() { return cpu_state_; }
    MipsTranslationCache* Cache()    { return cache_; }

private:
    MipsCpuState*         cpu_state_ = nullptr;
    MipsMmu*              mmu_       = nullptr;
    MipsTranslationCache* cache_     = nullptr;
    MipsProcessorConfig*  config_    = nullptr;
};
