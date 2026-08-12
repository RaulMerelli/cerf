#pragma once

#include <cstddef>
#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "block_context.h"

struct ArmCpuState;

class ArmCpu;
class ArmDecoder;
class ArmEmitServices;
class ArmMmu;
class ArmMmuProbe;
class ArmPageWalker;
class ArmRoutedInstruction;
class ArmTranslationCache;

class ArmBlockCompiler : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
    }

    void* Compile(uint32_t guest_pc);

    void SetPredecessor(uint32_t folded_va) { predecessor_va_ = folded_va; }

    static void __fastcall SctlrWriteHelper(uint32_t value,
                                            ArmBlockCompiler* compiler);
    static void __fastcall RaiseAbortDataHelper(uint32_t guest_pc,
                                                ArmBlockCompiler* compiler);
    static void __cdecl TraceDispatchPcHelper(ArmBlockCompiler* compiler,
                                              uint32_t pc);

private:
    uint32_t predecessor_va_ = 0;

    void   BuildTrampolines();
    void   Decode(uint32_t guest_pc, uint32_t folded_pc);
    size_t GenerateCode(uint8_t* code, uint8_t* code_end);

    BlockContext block_ctx_{};

    ArmTranslationCache*  cache_     = nullptr;
    ArmEmitServices*      emit_      = nullptr;
    ArmCpu*               cpu_       = nullptr;
    ArmCpuState*          cpu_state_ = nullptr;
    ArmDecoder*           decoder_   = nullptr;
    ArmMmu*               mmu_       = nullptr;
    ArmMmuProbe*          probe_     = nullptr;
    ArmPageWalker*        walker_    = nullptr;
    ArmRoutedInstruction* routed_    = nullptr;
};
