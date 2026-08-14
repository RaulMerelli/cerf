#pragma once

#include "../../core/service.h"

#include <cstdint>

class ArmProcessorConfig;
struct DecodedInsn;

class ThumbDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeThumb(DecodedInsn* insn, uint16_t op);

private:
    bool DecodeAddSubtract(DecodedInsn* insn, uint16_t op);
    bool DecodeAddToPcOrSp(DecodedInsn* insn, uint16_t op);
    bool DecodeAdjustStackPointer(DecodedInsn* insn, uint16_t op);
    bool DecodeAluOperations(DecodedInsn* insn, uint16_t op);
    bool DecodeBranchExchange(DecodedInsn* insn, uint16_t op);
    bool DecodeBranchLinkPrefix(DecodedInsn* insn, uint16_t op);
    bool DecodeBranchLinkSuffix(DecodedInsn* insn, uint16_t op);
    bool DecodeConditionalBranch(DecodedInsn* insn, uint16_t op);
    bool DecodeImmediateOffsetTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeImmediateOperations(DecodedInsn* insn, uint16_t op);
    bool DecodeLoadLiteral(DecodedInsn* insn, uint16_t op);
    bool DecodeMiscellaneous(DecodedInsn* insn, uint16_t op);
    bool DecodeRegisterOffsetTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeSpecialDataProcessing(DecodedInsn* insn, uint16_t op);
    bool DecodeStackRelativeTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeUnconditionalBranch(DecodedInsn* insn, uint16_t op);

    ArmProcessorConfig* processor_config_ = nullptr;
};
