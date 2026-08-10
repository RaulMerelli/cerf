#pragma once

#include <cstdint>

#include "../../core/service.h"

class ArmMmu;
class ArmPageWalker;
class PeripheralDispatcher;
struct ArmCpuState;

class ArmRoutedAccess : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Load(ArmCpuState* cpu_state, uint32_t guest_pc, uint32_t va,
              uint32_t bytes, uint32_t* out);
    bool Store(ArmCpuState* cpu_state, uint32_t guest_pc, uint32_t va,
               uint32_t bytes, uint32_t value);

private:
    [[noreturn]] void HaltUnalignedRouted(uint32_t guest_pc, uint32_t va,
                                          uint32_t bytes, uint32_t pa,
                                          const char* kind);
    [[noreturn]] void HaltRoutedWidth(uint32_t guest_pc, uint32_t va,
                                      uint32_t bytes, uint32_t pa,
                                      const char* kind);

    ArmMmu*               mmu_        = nullptr;
    ArmPageWalker*        walker_     = nullptr;
    PeripheralDispatcher* dispatcher_ = nullptr;
};
