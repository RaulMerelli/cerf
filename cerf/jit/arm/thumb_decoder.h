#pragma once

#include "../../core/service.h"

#include <cstdint>

class ArmProcessorConfig;
struct DecodedInsn;

class ThumbDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeThumb(DecodedInsn* insn, uint16_t op);

private:
    bool DecodeMiscellaneous(DecodedInsn* insn, uint16_t op);

    ArmProcessorConfig* processor_config_ = nullptr;
};
