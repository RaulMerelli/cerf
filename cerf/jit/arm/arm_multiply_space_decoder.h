#pragma once

#include "../../core/service.h"

class ArmProcessorConfig;
struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-7 multiply and multiply accumulate space
   (ARM DDI 0406C.c, p. A5-202) and the Table A5-9 halfword multiply
   space (p. A5-203). */
class ArmMultiplySpaceDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
    bool DecodeHalfword(DecodedInsn* insn, ArmOpcode op);

private:
    ArmProcessorConfig* processor_config_ = nullptr;
};
