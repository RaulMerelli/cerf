#include "../place_fns.h"

uint8_t* PlaceSingleDataTransferCALL(uint8_t*      cursor,
                                     DecodedInsn*  d,
                                     BlockContext* ctx) {
    cursor = PlacePushShadowStack(cursor, d, ctx);
    return PlaceSingleDataTransfer(cursor, d, ctx);
}
