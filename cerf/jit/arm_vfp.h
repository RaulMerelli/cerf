#pragma once

#include <cstdint>

#include "../core/service.h"

class ArmVfp : public Service {
public:
    using Service::Service;

    /* Flag bit packing for HandleBlockTransfer's `flags` arg. */
    static constexpr uint32_t kFlagL  = 1u << 0;  /* 1=load (VLDM), 0=store (VSTM) */
    static constexpr uint32_t kFlagW  = 1u << 1;  /* writeback */
    static constexpr uint32_t kFlagP  = 1u << 2;  /* pre-decrement */
    static constexpr uint32_t kFlagDp = 1u << 3;  /* cp_num=11 doubleword */

    /* Returns 0 on successful transfer (block continues), non-zero
       when UND or data abort fired (state has been redirected to
       the vector PC by Raise*; emit code MUST RETN the block on
       non-zero so the dispatcher picks up the vector). */
    uint32_t HandleBlockTransfer(uint32_t pc, uint32_t rn_idx, uint32_t vd,
                                 uint32_t imm8, uint32_t flags);

    static uint32_t __cdecl HandleBlockTransferHelper(ArmVfp*  vfp,
                                                      uint32_t pc,
                                                      uint32_t rn_idx,
                                                      uint32_t vd,
                                                      uint32_t imm8,
                                                      uint32_t flags);

    /* VLDR / VSTR — single-register VFP load/store. signed_off is
       the already-signed byte displacement from Rn (decoder applies
       the U-bit sign to d->offset). Same 0/non-zero status as
       HandleBlockTransfer. */
    uint32_t HandleSingleTransfer(uint32_t pc, uint32_t rn_idx, uint32_t vd,
                                  int32_t signed_off, uint32_t flags);

    static uint32_t __cdecl HandleSingleTransferHelper(ArmVfp*  vfp,
                                                      uint32_t pc,
                                                      uint32_t rn_idx,
                                                      uint32_t vd,
                                                      int32_t  signed_off,
                                                      uint32_t flags);

    uint32_t ExecuteCdp(uint32_t pc, uint32_t packed);

    static uint32_t __cdecl ExecuteCdpHelper(ArmVfp*  vfp,
                                             uint32_t pc,
                                             uint32_t packed);
};
