#include "../place_fns.h"

uint8_t* PlaceBx(uint8_t*      cursor,
                 DecodedInsn*  d,
                 BlockContext* ctx) {
    return PlaceBxImpl(cursor, d, ctx, /*is_call=*/false);
}
