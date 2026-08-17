#include <cstdint>

#include "../../../core/fatal.h"
#include "../arm_emit_services.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

namespace {

[[noreturn]] void CoprocUnimplementedFatalHelper(Fatal*   fatal,
                                                 uint32_t pc,
                                                 uint32_t cp_info,
                                                 uint32_t thumb) {
    fatal->Die("unimplemented %s coprocessor instruction at guest "
               "pc=0x%08X - p%u CRn=c%u CRm=c%u opc1=%u opc2=%u %s\n",
        thumb != 0u ? "Thumb" : "ARM",
        pc, (cp_info >> 24) & 0xFu, (cp_info >> 16) & 0xFu,
        (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0xFu, (cp_info >> 1) & 0x7u,
        (cp_info & 1u) ? "MRC/read" : "MCR/write");
}

[[noreturn]] void CoprocDataOperationUnimplementedFatalHelper(Fatal*   fatal,
                                                              uint32_t pc,
                                                              uint32_t cp_info,
                                                              uint32_t thumb) {
    fatal->Die("unimplemented %s coprocessor instruction at guest "
               "pc=0x%08X - CDP p%u CRd=c%u CRn=c%u CRm=c%u opc1=%u opc2=%u\n",
        thumb != 0u ? "Thumb" : "ARM",
        pc, (cp_info >> 20) & 0xFu, (cp_info >> 16) & 0xFu,
        (cp_info >> 12) & 0xFu, (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0xFu,
        (cp_info >> 1) & 0x7u);
}

[[noreturn]] void CoprocDataTransferUnimplementedFatalHelper(Fatal*   fatal,
                                                             uint32_t pc,
                                                             uint32_t cp_info,
                                                             uint32_t thumb) {
    fatal->Die("unimplemented %s coprocessor data transfer at guest "
               "pc=0x%08X - %s p%u CRd=c%u Rn=r%u P=%u U=%u D=%u W=%u\n",
        thumb != 0u ? "Thumb" : "ARM",
        pc, (cp_info & 1u) ? "LDC" : "STC", (cp_info >> 16) & 0xFu,
        (cp_info >> 12) & 0xFu, (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0x1u,
        (cp_info >> 3) & 0x1u, (cp_info >> 2) & 0x1u, (cp_info >> 1) & 0x1u);
}

}

uint8_t* EmitCoprocUnimplementedFatal(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    const uint32_t cp_info = (d->cp_num << 24) | (d->crn << 16) | (d->crm << 8) |
                             (d->cp_opc << 4) | (d->cp << 1) | (d->l ? 1u : 0u);
    EmitPush32(cursor, ctx->thumb ? 1u : 0u);
    EmitPush32(cursor, cp_info);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->FatalService())));
    EmitCall(cursor, reinterpret_cast<void*>(&CoprocUnimplementedFatalHelper));
    return cursor;
}

uint8_t* EmitCoprocDataOperationUnimplementedFatal(uint8_t* cursor, DecodedInsn* d,
                                                   BlockContext* ctx) {
    using namespace x86;
    const uint32_t cp_info = (d->cp_num << 20) | (d->crd << 16) |
                             (d->crn << 12) | (d->crm << 8) |
                             (d->cp_opc << 4) | (d->cp << 1);
    EmitPush32(cursor, ctx->thumb ? 1u : 0u);
    EmitPush32(cursor, cp_info);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->FatalService())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&CoprocDataOperationUnimplementedFatalHelper));
    return cursor;
}

uint8_t* EmitCoprocDataTransferUnimplementedFatal(uint8_t* cursor, DecodedInsn* d,
                                                  BlockContext* ctx) {
    using namespace x86;
    const uint32_t cp_info = (d->cp_num << 16) | (d->crd << 12) | (d->rn << 8) |
                             ((d->p & 1u) << 4) | ((d->u & 1u) << 3) |
                             ((d->n & 1u) << 2) | ((d->w & 1u) << 1) |
                             (d->l ? 1u : 0u);
    EmitPush32(cursor, ctx->thumb ? 1u : 0u);
    EmitPush32(cursor, cp_info);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->FatalService())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&CoprocDataTransferUnimplementedFatalHelper));
    return cursor;
}
