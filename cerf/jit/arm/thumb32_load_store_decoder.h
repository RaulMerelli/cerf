#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;

class Thumb32LoadStoreDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool DecodeLoadWord(DecodedInsn* insn, uint32_t op);

private:
    bool DecodeLoadLiteral(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadImmediate12(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadImmediate8(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadUnprivileged(DecodedInsn* insn, uint32_t op);
};
