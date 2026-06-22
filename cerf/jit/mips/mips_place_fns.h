#pragma once

#include "mips_decoded_insn.h"

/* r0 is hardwired zero: a place fn whose destination is gpr[0] must emit no
   store, or r0 stops reading as 0 and every zero-source idiom breaks. */

uint8_t* PlaceMipsUndefined(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsNop      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsAddu     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsLui      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsOri      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsOr       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsAddiu    (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsMtc0     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsMfc0     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsTlbwi    (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsJr       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsJalr     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsJ        (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsJal      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSw       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsBgtz     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsAddi     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsLw       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsDaddiu   (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSd       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsDsubu    (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSll      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsBeq      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsLd       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsAnd      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSltu     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsBne      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSltiu    (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsXor      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsAndi     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSubu     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSdr      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSdl      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsMovz     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSwr      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsDsll32   (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsDsrl32   (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsDaddu    (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsLwl      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSwl      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsLb       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSlti     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsBltz     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsBgez     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsAdd      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSh       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSb       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSrlv     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsSrl      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
