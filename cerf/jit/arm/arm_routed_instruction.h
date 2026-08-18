#pragma once

#include <cstdint>

#include "../../core/service.h"

class ArmCpu;
class ArmDecoder;
class ArmMmu;
class ArmPageWalker;
class ArmProcessorConfig;
class ArmRoutedAccess;
class ArmRoutedAddressing;
class Thumb32Decoder;
class ThumbDecoder;
struct ArmCpuState;
struct DecodedInsn;

class ArmRoutedInstruction : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void Complete(uint32_t guest_pc);

private:
    enum class Outcome { kNextInsn, kPcWritten, kExceptionEntered, kAborted };

    Outcome SingleTransfer(DecodedInsn* d);
    Outcome HalfwordTransfer(DecodedInsn* d);
    Outcome BlockTransfer(DecodedInsn* d);
    Outcome Swap(DecodedInsn* d);
    Outcome Exclusive(DecodedInsn* d, bool is_store);

    Outcome  Abort(DecodedInsn* d, bool wback, uint32_t base_on_abort);
    void     LoadWritePc(uint32_t value);

    ArmCpu*                   cpu_       = nullptr;
    ArmCpuState*              cpu_state_ = nullptr;
    ArmDecoder*               decoder_   = nullptr;
    ArmMmu*                   mmu_       = nullptr;
    ArmPageWalker*            walker_    = nullptr;
    ArmProcessorConfig*       config_    = nullptr;
    ArmRoutedAccess*          access_     = nullptr;
    ArmRoutedAddressing*      addressing_ = nullptr;
    ThumbDecoder*             thumb_     = nullptr;
    Thumb32Decoder*           thumb32_   = nullptr;
};
