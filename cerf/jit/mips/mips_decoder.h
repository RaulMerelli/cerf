#pragma once

#include <cstdint>

#include "mips_decoded_insn.h"

/* Decodes a 32-bit MIPS IV word into a MipsDecodedInsn (R/I/J fields +
   delay-slot classification). The engine's generate phase selects the emit
   (place_fn) from the decoded fields. */
class MipsDecoder {
public:
    /* Capability gates from MipsProcessorConfig (set once in MipsJit::OnReady);
       instruction recognition is config-driven, not hardcoded. */
    void Configure(bool has_fpu, bool has_llsc) {
        has_fpu_  = has_fpu;
        has_llsc_ = has_llsc;
    }

    /* Returns false for an opcode the running CPU does not implement (COP1 when
       !HasFpu, LL/SC when !HasLlsc, or a reserved encoding) - the engine then
       raises Reserved/Coprocessor-Unusable for it. */
    bool Decode(uint32_t word, uint32_t pc, MipsDecodedInsn* d);

private:
    bool has_fpu_  = false;
    bool has_llsc_ = false;
};
