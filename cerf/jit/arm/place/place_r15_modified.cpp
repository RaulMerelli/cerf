#include "../place_fns.h"
#include "../../x86_emit.h"

uint8_t* PlaceR15ModifiedHelper(uint8_t* cursor, DecodedInsn* /*d*/,
                                BlockContext* /*ctx*/) {
    x86::EmitRet(cursor);
    return cursor;
}
