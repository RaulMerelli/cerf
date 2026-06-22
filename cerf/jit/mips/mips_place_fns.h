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
uint8_t* PlaceMipsJr       (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsJalr     (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsJ        (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
uint8_t* PlaceMipsJal      (uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
