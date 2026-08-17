#pragma once

#include "../../core/service.h"

#include <cstdint>

struct DecodedInsn;

class Thumb32Fatal : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    [[noreturn]] void Unimplemented(const char* what, const DecodedInsn* insn,
                                    uint32_t op);
};
