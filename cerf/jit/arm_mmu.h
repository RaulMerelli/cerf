#pragma once

#include <cstdint>
#include <optional>

#include "../core/service.h"
#include "arm_mmu_state.h"
#include "cpu_state.h"

class ArmJit;
class ArmProcessorConfig;
class EmulatedMemory;

class ArmMmu : public Service {
public:
    using Service::Service;
    ~ArmMmu() override;

    void OnReady() override;

    ArmMmuState* State() { return &state_; }

    /* On nullptr return, check io_pending_address(): zero ⇒ genuine
       fault (FAR/FSR set, caller raises abort); non-zero ⇒ PA lies
       in peripheral space, caller routes to PeripheralDispatcher. */
    uint8_t* TranslateRead    (ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint);
    uint8_t* TranslateWrite   (ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint);
    uint8_t* TranslateReadWrite(ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint);
    uint8_t* TranslateExecute (ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint);

    /* No walk, no TLB fill, no abort raise — diagnostic-only. */
    std::optional<uint8_t*> PeekDataTlb(uint32_t va) const;

    uint32_t io_pending_address() const { return io_pending_address_; }
    uint32_t io_pending_address_adjust() const { return io_pending_address_adjust_; }

    uint32_t* IoPendingAddressPtr()       { return &io_pending_address_; }
    uint32_t* IoPendingAddressAdjustPtr() { return &io_pending_address_adjust_; }

    /* cp15 c0 op1=1 CRm=0 op2=0 (CCSIDR), indexed by CSSELR.
       Called from JIT only when HasCp15V7() is true.
       __fastcall: ECX = mmu pointer; return in EAX. */
    static uint32_t __fastcall CcsidrLookupHelper(ArmMmu* mmu);

private:
    template <ArmMmuAccess kAccess>
    uint8_t* MapGuestVirtualToHost(ArmCpuState* cpu_state, uint32_t p, int8_t* tlb_index_hint);

    void RaiseAbort(uint32_t va, uint32_t fault_status, bool is_write);

    /* PA 0 encodes as (io_pa=1, adjust=-1) so io_pa==0 stays the
       "no IO, real fault" sentinel. */
    void SetIoPending(uint32_t pa);

    ArmMmuState         state_{};
    EmulatedMemory*     memory_           = nullptr;
    ArmProcessorConfig* processor_config_ = nullptr;

    uint32_t io_pending_address_        = 0;
    uint32_t io_pending_address_adjust_ = 0;
};
