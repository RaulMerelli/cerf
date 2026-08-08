#pragma once

#include "../../core/service.h"

class ArmProcessorConfig;
struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-22 coprocessor instructions and Supervisor
   Call space (ARM DDI 0406C.c, p. A5-215). */
class ArmCoprocSpaceDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);

private:
    ArmProcessorConfig* processor_config_ = nullptr;
};
