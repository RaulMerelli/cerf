#include "../place_fns.h"

uint8_t* PlaceIllegalCoproc(uint8_t*      cursor,
                            DecodedInsn*  d,
                            BlockContext* ctx) {
    /* place_fn assigned by the decoder when no recognized coprocessor
       handler matches. Same UND-raise-and-return body the cp15 LDC /
       STC / CDP no-coproc paths emit, delegated to the shared helper. */
    return EmitRaiseUndAndReturn(cursor, d, ctx);
}
