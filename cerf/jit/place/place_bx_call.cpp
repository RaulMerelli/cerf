#include "../place_fns.h"

uint8_t* PlaceBxCALL(uint8_t*      cursor,
                     DecodedInsn*  d,
                     BlockContext* ctx) {
    cursor = PlacePushShadowStack(cursor, d, ctx);
    return PlaceBxImpl(cursor, d, ctx, /*is_call=*/true);
}
