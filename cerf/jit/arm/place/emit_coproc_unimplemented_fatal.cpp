#include <cstdint>

#include "../arm_jit.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../../core/log.h"

namespace {

[[noreturn]] void CoprocUnimplementedFatalHelper(uint32_t pc, uint32_t cp_info) {
    LOG(Jit, "FATAL: unimplemented coprocessor instruction at guest "
             "pc=0x%08X - p%u CRn=c%u CRm=c%u opc1=%u opc2=%u %s\n",
        pc, (cp_info >> 24) & 0xFu, (cp_info >> 16) & 0xFu,
        (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0xFu, (cp_info >> 1) & 0x7u,
        (cp_info & 1u) ? "MRC/read" : "MCR/write");
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

[[noreturn]] void CoprocDataOperationUnimplementedFatalHelper(uint32_t pc,
                                                              uint32_t cp_info) {
    LOG(Jit, "FATAL: unimplemented coprocessor instruction at guest "
             "pc=0x%08X - CDP p%u CRd=c%u CRn=c%u CRm=c%u opc1=%u opc2=%u\n",
        pc, (cp_info >> 20) & 0xFu, (cp_info >> 16) & 0xFu,
        (cp_info >> 12) & 0xFu, (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0xFu,
        (cp_info >> 1) & 0x7u);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

[[noreturn]] void CoprocDataTransferUnimplementedFatalHelper(uint32_t pc,
                                                             uint32_t cp_info) {
    LOG(Jit, "FATAL: unimplemented coprocessor data transfer at guest "
             "pc=0x%08X - %s p%u CRd=c%u Rn=r%u P=%u U=%u D=%u W=%u\n",
        pc, (cp_info & 1u) ? "LDC" : "STC", (cp_info >> 16) & 0xFu,
        (cp_info >> 12) & 0xFu, (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0x1u,
        (cp_info >> 3) & 0x1u, (cp_info >> 2) & 0x1u, (cp_info >> 1) & 0x1u);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

}  // namespace

uint8_t* EmitCoprocUnimplementedFatal(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    (void)ctx;
    const uint32_t cp_info = (d->cp_num << 24) | (d->crn << 16) | (d->crm << 8) |
                             (d->cp_opc << 4) | (d->cp << 1) | (d->l ? 1u : 0u);
    EmitPush32(cursor, cp_info);
    EmitPush32(cursor, d->guest_address);
    EmitCall(cursor, reinterpret_cast<void*>(&CoprocUnimplementedFatalHelper));
    return cursor;
}

uint8_t* EmitCoprocDataOperationUnimplementedFatal(uint8_t* cursor, DecodedInsn* d,
                                                   BlockContext* ctx) {
    using namespace x86;
    (void)ctx;
    const uint32_t cp_info = (d->cp_num << 20) | (d->crd << 16) |
                             (d->crn << 12) | (d->crm << 8) |
                             (d->cp_opc << 4) | (d->cp << 1);
    EmitPush32(cursor, cp_info);
    EmitPush32(cursor, d->guest_address);
    EmitCall(cursor,
        reinterpret_cast<void*>(&CoprocDataOperationUnimplementedFatalHelper));
    return cursor;
}

uint8_t* EmitCoprocDataTransferUnimplementedFatal(uint8_t* cursor, DecodedInsn* d,
                                                  BlockContext* ctx) {
    using namespace x86;
    (void)ctx;
    const uint32_t cp_info = (d->cp_num << 16) | (d->crd << 12) | (d->rn << 8) |
                             ((d->p & 1u) << 4) | ((d->u & 1u) << 3) |
                             ((d->n & 1u) << 2) | ((d->w & 1u) << 1) |
                             (d->l ? 1u : 0u);
    EmitPush32(cursor, cp_info);
    EmitPush32(cursor, d->guest_address);
    EmitCall(cursor,
        reinterpret_cast<void*>(&CoprocDataTransferUnimplementedFatalHelper));
    return cursor;
}
