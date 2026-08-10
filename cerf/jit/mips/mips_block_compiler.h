#pragma once

#include <cstddef>
#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "mips_block_context.h"

struct MipsCpuState;
struct JitBlock;

class MipsExceptionDelivery;
class MipsInsnDecoder;
class MipsMmu;
class MipsTranslationCache;

class MipsBlockCompiler : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    void* FindBlockNativeStart(uint32_t guest_pc);
    void* Compile(uint32_t guest_pc);

    static void __cdecl UnimplementedHelper(MipsBlockCompiler* c, uint32_t pc,
                                            uint32_t raw);

    /* Emitted before a block-leading PC-relative MIPS16 insn: loud-fatals when
       the block is entered as a pending jump's delay slot, whose Table 3-12
       p67 base (the jump's PC) the block-local decode cannot know. */
    static void __cdecl PcrelDelaySlotHelper(MipsBlockCompiler* c, uint32_t pc);

    static void __cdecl TraceDispatchPcHelper(MipsBlockCompiler* c, uint32_t pc);

    /* QEMU target/mips gen_branch: after a delay slot, set pc from
       branch_state/btarget/bcond, clear it, and return 1 (resolved) / 0. */
    static int __fastcall ResolveBranchHelper(uint32_t fallthrough,
                                              MipsBlockCompiler* c);

    /* QEMU target/mips decode_opc "blikely not taken". */
    static int __fastcall NullifyLikelyHelper(uint32_t fallthrough,
                                              MipsBlockCompiler* c);

private:
    bool   TailStillMapped(const JitBlock* blk);
    size_t GenerateCode(uint8_t* code_location);

    MipsBlockContext block_ctx_{};

    MipsCpuState*          cpu_state_  = nullptr;
    MipsMmu*               mmu_        = nullptr;
    MipsTranslationCache*  cache_      = nullptr;
    MipsInsnDecoder*       decoder_    = nullptr;
    MipsExceptionDelivery* exceptions_ = nullptr;
};
