#include "../arm_emit_services.h"
#include "../coproc_emitter.h"
#include "../place_fns.h"

uint8_t* PlaceCoprocDataTransfer(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx);
    return ctx->emit->Coproc()->EmitDataTransfer(cursor, d, ctx);
}

uint8_t* PlaceCoprocDataOperation(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) {
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx);
    return ctx->emit->Coproc()->EmitDataOperation(cursor, d, ctx);
}
