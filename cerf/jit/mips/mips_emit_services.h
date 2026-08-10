#pragma once

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"

struct MipsCpuState;

class MipsBlockCompiler;
class MipsCp0Emitter;
class MipsCpu;
class MipsCp0Ops;
class MipsExceptionDelivery;
class MipsInterruptChannel;
class MipsMemoryAccess;
class MipsMmu;
class MipsProcessorConfig;
class MipsTranslationCache;
class MipsWideArithmetic;
class PeripheralDispatcher;

class MipsEmitServices : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    MipsCpu*              Cpu()        { return cpu_; }
    MipsCpuState*         CpuState()   { return cpu_state_; }
    MipsMmu*              Mmu()        { return mmu_; }
    MipsProcessorConfig*  CpuConfig()  { return cpu_config_; }
    MipsCp0Emitter*       Cp0Emitter() { return cp0_emitter_; }
    PeripheralDispatcher* Peripheral() { return peripheral_; }

    MipsTranslationCache*  TranslationCache() { return cache_; }
    MipsMemoryAccess*      Memory()           { return memory_access_; }
    MipsWideArithmetic*    WideArithmetic()   { return wide_; }
    MipsCp0Ops*            Cp0Ops()           { return cp0_ops_; }
    MipsExceptionDelivery* Exceptions()       { return exceptions_; }
    MipsInterruptChannel*  InterruptChannel() { return channel_; }

private:
    MipsCpu*              cpu_         = nullptr;
    MipsCpuState*         cpu_state_   = nullptr;
    MipsMmu*              mmu_         = nullptr;
    MipsProcessorConfig*  cpu_config_  = nullptr;
    MipsCp0Emitter*       cp0_emitter_ = nullptr;
    PeripheralDispatcher* peripheral_  = nullptr;

    MipsTranslationCache*  cache_         = nullptr;
    MipsMemoryAccess*      memory_access_ = nullptr;
    MipsWideArithmetic*    wide_          = nullptr;
    MipsCp0Ops*            cp0_ops_       = nullptr;
    MipsExceptionDelivery* exceptions_    = nullptr;
    MipsInterruptChannel*  channel_       = nullptr;
};
