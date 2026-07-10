#pragma once

#include <cstdint>

#include "../../core/service.h"

struct MipsDecodedInsn;
struct MipsBlockContext;

/* Per-SoC MFC0/MTC0/DMFC0/DMTC0 emit strategy, selected by GetSoc(). The base
   supplies the generic R4x00 CP0-move implementation (sel-0 32-bit CP0 model,
   with the Count/Compare/EntryHi side-effect helpers); a SoC whose CP0 moves
   genuinely differ overrides the relevant method. */
class MipsCp0Emitter : public Service {
public:
    using Service::Service;

    /* MFC0 rt, rd, sel: gpr[rt] = sext32(cp0[rd]) (32-bit move from CP0). */
    virtual uint8_t* EmitMfc0(uint8_t* cursor, MipsDecodedInsn* d,
                              MipsBlockContext* ctx);

    /* MTC0 rt, rd, sel: cp0[rd] = gpr[rt][31:0] (32-bit move to CP0), with any
       per-register side effects (timer re-anchor, ASID-change jump-cache flush). */
    virtual uint8_t* EmitMtc0(uint8_t* cursor, MipsDecodedInsn* d,
                              MipsBlockContext* ctx);

    /* DMFC0 rt, rd, sel: the 64-bit move from CP0 (32-bit CP0 model -> sext32). */
    virtual uint8_t* EmitDmfc0(uint8_t* cursor, MipsDecodedInsn* d,
                               MipsBlockContext* ctx);

    /* DMTC0 rt, rd, sel: the 64-bit move to CP0. */
    virtual uint8_t* EmitDmtc0(uint8_t* cursor, MipsDecodedInsn* d,
                               MipsBlockContext* ctx);

protected:
    /* CP0 register number -> MipsCpuState field offset, or -1 for a register the
       core does not implement. */
    virtual int32_t RegOffset(uint32_t rd) const;

    /* True iff MTC0 may write cp0[rd] on this core. */
    virtual bool RegWritable(uint32_t rd) const;

    /* The `void __fastcall(uint32_t value, MipsJit*)` run for an MTC0 whose
       write carries a side effect, or nullptr for a plain store to RegOffset. */
    virtual void* Mtc0Helper(uint32_t rd) const;

    /* gpr[rt] = sext32(cp0[rd]), sel 0 only; shared by MFC0 and DMFC0. */
    uint8_t* EmitFromCop0(uint8_t* cursor, MipsDecodedInsn* d,
                          MipsBlockContext* ctx);

    /* cp0[rd] = gpr[rt][31:0], sel 0 only; is_dword fatals on genuine 64-bit
       CP0 state the 32-bit model cannot hold. */
    uint8_t* EmitToCop0(uint8_t* cursor, MipsDecodedInsn* d,
                        MipsBlockContext* ctx, bool is_dword);
};
