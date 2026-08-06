#pragma once

#include "../../core/service.h"

struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-21 branch/block space (ARM DDI 0406C.c,
   p. A5-214). */
class ArmBranchBlockSpaceDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);
};
