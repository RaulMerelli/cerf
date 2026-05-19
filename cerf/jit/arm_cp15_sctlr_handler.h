#pragma once

#include <cstdint>

#include "../core/service.h"

class ArmJit;
class ArmMmu;
class EmulatedMemory;

class ArmCp15SctlrHandler : public Service {
public:
    using Service::Service;
    ~ArmCp15SctlrHandler() override;

    void OnReady() override;

    /* Per-instance JIT trampoline that cp15 c1 SCTLR-write emit
       sites CALL into. Body baked with the live ArmJit pointer
       in InitializeTrampoline. */
    void* Trampoline() { return trampoline_; }

    void InitializeTrampoline(ArmJit* jit);

    int HandleWrite(ArmJit*  jit,
                    uint32_t new_value,
                    uint32_t guest_addr);

    /* Static __cdecl thunk used by the trampoline as its CALL
       target. Forwards to HandleWrite on the live ArmCp15SctlrHandler
       instance baked into the trampoline. */
    static int __cdecl HandleWriteStaticHelper(ArmCp15SctlrHandler* self,
                                               ArmJit*              jit,
                                               uint32_t             new_value,
                                               uint32_t             guest_addr);

private:
    ArmMmu*         mmu_        = nullptr;
    EmulatedMemory* memory_     = nullptr;
    uint8_t*        trampoline_ = nullptr;
};
