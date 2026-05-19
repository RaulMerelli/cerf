#include "../place_fns.h"

uint8_t* PlaceNop(uint8_t*      cursor,
                  DecodedInsn*  /* d */,
                  BlockContext* /* ctx */) {
    return cursor;
}
