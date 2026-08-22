#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;

class Thumb32LoadStoreDualDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    bool has_cp15_v7_ = false;

    bool DecodeDual(DecodedInsn* insn, uint32_t op);
    bool DecodeExclusiveOrTableBranch(DecodedInsn* insn, uint32_t op);
    bool DecodeTableBranchByte(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadExclusive(DecodedInsn* insn, uint32_t op, uint32_t bytes);
    bool DecodeStoreExclusive(DecodedInsn* insn, uint32_t op, uint32_t bytes);
};
