#pragma once

#include "../../core/service.h"

#include <cstdint>

class ArmProcessorConfig;
struct DecodedInsn;

class ThumbStackControlDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeStackControlGroup(DecodedInsn* insn, uint16_t op);

private:
    bool DecodeAdjustStackPointer(DecodedInsn* insn, uint16_t op);
    bool DecodeCompareAndBranch(DecodedInsn* insn, uint16_t op);
    bool DecodeIfThen(DecodedInsn* insn, uint16_t op);

    ArmProcessorConfig* processor_config_ = nullptr;
};
