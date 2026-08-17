#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;
class ArmProcessorConfig;
class ArmDecoder;

class Thumb32Decoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* ARM DDI 0406C.c A6.1 (p. A6-220): "If the value of bits[15:11] of the
       halfword being decoded is one of the following, the halfword is the first
       halfword of a 32-bit instruction: 0b11101, 0b11110, 0b11111. Otherwise,
       the halfword is a 16-bit instruction." */
    bool IsWide(uint16_t first_halfword) const {
        const uint32_t top5 = (first_halfword >> 11) & 0x1Fu;
        return has_thumb2_ &&
               (top5 == 0x1Du || top5 == 0x1Eu || top5 == 0x1Fu);
    }

    bool DecodeThumb32(DecodedInsn* insn, uint32_t op);

private:
    bool                has_thumb2_ = false;
    ArmProcessorConfig* processor_config_ = nullptr;
    ArmDecoder*         arm_decoder_ = nullptr;

    bool DecodeBranchesMiscControl(DecodedInsn* insn, uint32_t op);
    bool DecodeCoprocessorSimdFp(DecodedInsn* insn, uint32_t op);
    bool DecodeDataProcessingModifiedImmediate(DecodedInsn* insn, uint32_t op);
    bool DecodeDataProcessingPlainBinaryImmediate(DecodedInsn* insn, uint32_t op);
    bool DecodeDataProcessingRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeDataProcessingShiftedRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadByteMemoryHints(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadHalfwordMemoryHints(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadStoreDualExclusiveTableBranch(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadStoreMultiple(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadWord(DecodedInsn* insn, uint32_t op);
    bool DecodeLongMultiplyDivide(DecodedInsn* insn, uint32_t op);
    bool DecodeMultiplyAbsoluteDifference(DecodedInsn* insn, uint32_t op);
    bool DecodeSimdElementOrStructure(DecodedInsn* insn, uint32_t op);
    bool DecodeStoreSingleDataItem(DecodedInsn* insn, uint32_t op);

    uint32_t ThumbExpandImm(uint32_t key, uint32_t imm8) const;

    [[noreturn]] void Unimplemented(const char* what, const DecodedInsn* insn,
                                    uint32_t op);
};
