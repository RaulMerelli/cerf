#pragma once

#include <cstdint>

#include "mips_decoded_insn.h"

/* Decodes a 32-bit MIPS IV word into a MipsDecodedInsn (R/I/J fields +
   delay-slot classification). The engine's generate phase selects the emit
   (place_fn) from the decoded fields. */
class MipsDecoder {
public:
    /* Returns false for an opcode outside the MIPS IV integer set this kernel
       uses (COP1/FPU included, since the soft-float build never enables CU1) -
       the engine then raises Reserved/Coprocessor-Unusable. */
    bool Decode(uint32_t word, uint32_t pc, MipsDecodedInsn* d);
};
