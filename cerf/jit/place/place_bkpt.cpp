#include "../place_fns.h"

uint8_t* PlaceBKPT(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext* ctx) {
    /* Software-breakpoint encoded in the guest instruction stream.
       Behaves as a prefetch-abort. */
    return PlaceRaiseAbortPrefetchException(cursor, d, ctx);
}
