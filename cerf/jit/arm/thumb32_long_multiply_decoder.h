#pragma once

#include "../../core/service.h"

#include <cstdint>

class Thumb32Fatal;
struct DecodedInsn;

class Thumb32LongMultiplyDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeLongMultiplyDivide(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_               = nullptr;
    bool          has_integer_divide_  = false;

    bool RegistersValid(uint32_t op) const;
    bool DecodeLongMultiply(DecodedInsn* insn, uint32_t op, uint32_t row);
    bool DecodeHalfwordLongMultiply(DecodedInsn* insn, uint32_t op);
    bool DecodeDivide(DecodedInsn* insn, uint32_t op, bool is_signed);
};
