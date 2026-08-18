#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;

class Thumb32LoadStoreMultipleDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    bool DecodeBlockTransfer(DecodedInsn* insn, uint32_t op, bool increment);
    bool DecodeReturnFromException(DecodedInsn* insn, uint32_t op,
                                   bool increment);
    bool DecodeStoreReturnState(DecodedInsn* insn, uint32_t op, bool increment);
};
