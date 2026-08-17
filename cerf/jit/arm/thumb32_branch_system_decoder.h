#pragma once

#include "../../core/service.h"

#include <cstdint>

class Thumb32Fatal;
struct DecodedInsn;

class Thumb32BranchSystemDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_ = nullptr;

    int32_t BranchOffset25(uint32_t op) const;

    bool DecodeBranch(DecodedInsn* insn, uint32_t op);
    bool DecodeBranchLink(DecodedInsn* insn, uint32_t op, bool exchange);
    bool DecodeConditionalBranch(DecodedInsn* insn, uint32_t op);
    bool DecodeCpsAndHints(DecodedInsn* insn, uint32_t op);
    bool DecodeExceptionReturn(DecodedInsn* insn, uint32_t op);
    bool DecodeControlInstructions(DecodedInsn* insn, uint32_t op);
    bool DecodeMoveFromSpecial(DecodedInsn* insn, uint32_t op);
    bool DecodeMoveToSpecial(DecodedInsn* insn, uint32_t op);
    bool DecodeSystemGroup(DecodedInsn* insn, uint32_t op);
};
