#include "../mips_place_fns.h"

/* NOP (encoded SLL r0,r0,0) has no architectural effect - emit nothing. */
uint8_t* PlaceMipsNop(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext*) {
    return cursor;
}
