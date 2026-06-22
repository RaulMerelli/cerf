#include "../arm_jit.h"
#include "../coproc_emitter.h"
#include "../place_fns.h"

uint8_t* PlaceCoprocRegisterTransfer(uint8_t*      cursor,
                                     DecodedInsn*  d,
                                     BlockContext* ctx) {
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx);
    return ctx->jit->Coproc()->EmitRegisterTransfer(cursor, d, ctx);
}
