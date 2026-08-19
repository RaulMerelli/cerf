#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;

enum class StoreSize { kWord, kByte, kHalfword };

class Thumb32LoadStoreDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool DecodeLoadWord(DecodedInsn* insn, uint32_t op);
    bool DecodeStoreSingleDataItem(DecodedInsn* insn, uint32_t op);

private:
    bool DecodeLoadLiteral(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadImmediate12(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadImmediate8(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadUnprivileged(DecodedInsn* insn, uint32_t op);
    bool DecodeStoreImmediate12(DecodedInsn* insn, uint32_t op, StoreSize size);
    bool DecodeStoreImmediate8(DecodedInsn* insn, uint32_t op, StoreSize size);
    bool DecodeStoreRegister(DecodedInsn* insn, uint32_t op, StoreSize size);
    bool DecodeStoreUnprivileged(DecodedInsn* insn, uint32_t op, StoreSize size);
    void SetStoreTransfer(DecodedInsn* insn, StoreSize size);
};
