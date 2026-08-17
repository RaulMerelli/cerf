#pragma once

#include "../../core/service.h"

#include <cstdint>

struct ArmCpuState;
struct DecodedInsn;

class ArmRoutedAddressing : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t PcReadValue(const DecodedInsn* d) const;
    uint32_t SingleShiftedOffset(const DecodedInsn* d) const;
    uint32_t SingleOffsetAddr(const DecodedInsn* d) const;
    uint32_t HalfwordOffsetAddr(const DecodedInsn* d) const;

private:
    ArmCpuState* cpu_state_ = nullptr;
};
