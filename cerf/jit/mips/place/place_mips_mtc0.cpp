#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* MTC0 rt, rd : cp0[rd] = gpr[rt] (low 32 bits; MTC0 is a 32-bit move), sel 0. */
uint8_t* PlaceMipsMtc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;

    if ((d->raw & 0x7u) != 0u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }

    int32_t off;
    switch (d->rd) {
        case MipsCp0::kIndex:    off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_index));    break;
        case MipsCp0::kEntryLo0: off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_entrylo0)); break;
        case MipsCp0::kEntryLo1: off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_entrylo1)); break;
        case MipsCp0::kContext:  off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_context));  break;
        case MipsCp0::kPageMask: off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_pagemask)); break;
        case MipsCp0::kWired:    off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_wired));    break;
        case MipsCp0::kCount:    off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_count));    break;
        case MipsCp0::kEntryHi:  off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_entryhi));  break;
        case MipsCp0::kCompare:  off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_compare));  break;
        case MipsCp0::kStatus:   off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_status));   break;
        case MipsCp0::kCause:    off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_cause));    break;
        case MipsCp0::kEPC:      off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_epc));      break;
        case MipsCp0::kConfig:   off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_config));   break;
        case MipsCp0::kErrorEPC: off = static_cast<int32_t>(offsetof(MipsCpuState, cp0_errorepc)); break;
        default:
            return PlaceMipsUndefined(cursor, d, ctx);
    }

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovBaseDisp32Reg(cursor, kStateReg, off, kEax);
    return cursor;
}
