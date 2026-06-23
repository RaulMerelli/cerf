#pragma once

#include <cstdint>

#include "../../core/service.h"

struct MipsDecodedInsn;
struct MipsBlockContext;

class MipsCp0Emitter : public Service {
public:
    using Service::Service;

    /* MFC0 rt, rd, sel: gpr[rt] = sext32(cp0[rd]) (32-bit move from CP0). */
    virtual uint8_t* EmitMfc0(uint8_t* cursor, MipsDecodedInsn* d,
                              MipsBlockContext* ctx) = 0;

    /* MTC0 rt, rd, sel: cp0[rd] = gpr[rt][31:0] (32-bit move to CP0), with any
       per-register side effects (timer re-anchor, ASID-change jump-cache flush). */
    virtual uint8_t* EmitMtc0(uint8_t* cursor, MipsDecodedInsn* d,
                              MipsBlockContext* ctx) = 0;

    /* DMFC0 rt, rd, sel: gpr[rt] = cp0[rd], the full 64-bit move from CP0. */
    virtual uint8_t* EmitDmfc0(uint8_t* cursor, MipsDecodedInsn* d,
                               MipsBlockContext* ctx) = 0;

    /* DMTC0 rt, rd, sel: cp0[rd] = gpr[rt], the full 64-bit move to CP0. */
    virtual uint8_t* EmitDmtc0(uint8_t* cursor, MipsDecodedInsn* d,
                               MipsBlockContext* ctx) = 0;
};
