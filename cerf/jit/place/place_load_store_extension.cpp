#include <cstdint>

#include "../place_fns.h"

uint8_t* PlaceLoadStoreExtension(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    if (d->op1 == 0) {
        return EmitSwap(cursor, d, ctx);
    }
    return EmitHalfwordSignedTransfer(cursor, d, ctx);
}
