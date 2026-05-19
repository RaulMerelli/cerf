#include "../place_fns.h"

uint8_t* PlaceEntrypointMiddle(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) {
    /* Deliver pending interrupts first, then fall through into the
       next basic block in the same compile batch. */
    cursor = PlaceInterruptPoll(cursor, d, ctx);
    return cursor;
}
