#include <cstdint>

#include "../decoded_insn.h"
#include "../place_fns.h"

uint8_t* PlaceNeonUnimplemented(uint8_t*      cursor,
                                DecodedInsn*  d,
                                BlockContext* ctx) {
    return PlaceArmUnimplemented(cursor, d, ctx);
}
