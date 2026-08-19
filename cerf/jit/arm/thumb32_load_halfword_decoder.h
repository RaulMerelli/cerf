#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;

class Thumb32LoadHalfwordDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool DecodeLoadHalfwordMemoryHints(DecodedInsn* insn, uint32_t op);

private:
    bool DecodeLiteral(DecodedInsn* insn, uint32_t op, uint32_t ext_op);
    bool DecodeImmediate12(DecodedInsn* insn, uint32_t op, uint32_t ext_op);
    bool DecodeImmediate8(DecodedInsn* insn, uint32_t op, uint32_t ext_op);
    bool DecodeRegister(DecodedInsn* insn, uint32_t op, uint32_t ext_op);
    bool DecodeUnprivileged(DecodedInsn* insn, uint32_t op, uint32_t ext_op);
};
