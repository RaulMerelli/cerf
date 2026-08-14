#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;

class ThumbTransferDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool DecodeImmediateOffsetTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeLoadLiteral(DecodedInsn* insn, uint16_t op);
    bool DecodeRegisterOffsetTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeStackRelativeTransfer(DecodedInsn* insn, uint16_t op);
};
