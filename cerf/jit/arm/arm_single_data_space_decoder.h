#pragma once

#include "../../core/service.h"

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-15 load/store word and unsigned byte space
   (ARM DDI 0406C.c, p. A5-208). */
class ArmSingleDataSpaceDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};
