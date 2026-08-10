#include "../arm_emit_services.h"
#include "../coproc_emitter.h"
#include "../place_fns.h"

uint8_t* PlaceCoprocRegisterTransfer(uint8_t*      cursor,
                                     DecodedInsn*  d,
                                     BlockContext* ctx) {
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx);
    return ctx->emit->Coproc()->EmitRegisterTransfer(cursor, d, ctx);
}
