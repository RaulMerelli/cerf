#pragma once

#include <cstdint>

#include "block_context.h"
#include "decoded_insn.h"

/* PLD hint, ARM no-op encodings, instruction-level no-ops. Emits
   no host code; the cursor passes through unchanged. */
uint8_t* TranslateNop(uint8_t* code_cursor, DecodedInsn* d, BlockContext* ctx);

uint8_t* PlaceConditionCheck(uint8_t* cursor, const DecodedInsn* d, BlockContext* ctx);

/* Back-patch every pending skip-fixup label registered by
   PlaceConditionCheck since the last call. Invoked by the
   orchestrator at every cond-run boundary (cond changed, entrypoint
   changed, end of block). Resets ctx->big_skip_count to 0. */
void PlaceEndConditionCheck(uint8_t* cursor, BlockContext* ctx);
