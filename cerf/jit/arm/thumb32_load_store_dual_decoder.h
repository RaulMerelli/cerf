#pragma once

#include "../../core/service.h"

#include <cstdint>

class Thumb32Fatal;
struct DecodedInsn;

class Thumb32LoadStoreDualDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_ = nullptr;

    bool DecodeDual(DecodedInsn* insn, uint32_t op);
    bool DecodeExclusiveOrTableBranch(DecodedInsn* insn, uint32_t op);
    bool DecodeTableBranchByte(DecodedInsn* insn, uint32_t op);
};
