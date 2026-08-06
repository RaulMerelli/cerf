#pragma once

#include "../../core/service.h"

class ArmProcessorConfig;
class NeonUnconditionalDecoder;
struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-23 cond == 1111 unconditional space
   (ARM DDI 0406C.c, p. A5-216). */
class ArmUnconditionalSpaceDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);

private:
    ArmProcessorConfig*       processor_config_           = nullptr;
    NeonUnconditionalDecoder* neon_unconditional_decoder_ = nullptr;
};
