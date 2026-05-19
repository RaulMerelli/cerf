#include "../place_fns.h"

uint8_t* PlaceCoprocExtension(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    return EmitRaiseUndAndReturn(cursor, d, ctx);
}
