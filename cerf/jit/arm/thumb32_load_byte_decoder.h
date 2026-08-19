#pragma once

#include "../../core/service.h"

#include <cstdint>

class Thumb32Fatal;
struct DecodedInsn;

class Thumb32LoadByteDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeLoadByteMemoryHints(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_ = nullptr;

    bool DecodePreloadRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteImmediate12(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteImmediate8(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteUnprivileged(DecodedInsn* insn, uint32_t op);
};
