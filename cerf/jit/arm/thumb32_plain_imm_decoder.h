#pragma once

#include "../../core/service.h"
#include "decoded_insn.h"

#include <cstdint>

class Thumb32Fatal;

class Thumb32PlainImmDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_ = nullptr;

    bool DecodeAddSubImm12(DecodedInsn* insn, uint32_t op, bool add);
    bool DecodeMoveImm16(DecodedInsn* insn, uint32_t op, ArmPlaceFn place);
    bool DecodeBitFieldExtract(DecodedInsn* insn, uint32_t op, ArmPlaceFn place);
    bool DecodeBitFieldInsert(DecodedInsn* insn, uint32_t op);
    bool DecodeSaturate(DecodedInsn* insn, uint32_t op);
};
