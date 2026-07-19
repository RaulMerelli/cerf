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
    void Configure(bool has_fpu, bool has_llsc, bool has_mips4,
                   bool has_vr41xx_power_modes, bool has_64bit, bool has_eret,
                   bool has_rfe, bool has_mips16) {
        has_fpu_   = has_fpu;
        has_llsc_  = has_llsc;
        has_mips4_ = has_mips4;
        has_vr41xx_power_modes_ = has_vr41xx_power_modes;
        has_64bit_ = has_64bit;
        has_eret_  = has_eret;
        has_rfe_   = has_rfe;
        has_mips16_ = has_mips16;
    }

    /* Returns false for an opcode the running CPU does not implement (COP1 when
       !HasFpu, LL/SC when !HasLlsc, or a reserved encoding) - the engine then
       raises Reserved/Coprocessor-Unusable for it. */
    bool Decode(uint32_t word, uint32_t pc, MipsDecodedInsn* d);

private:
    bool has_fpu_   = false;
    bool has_llsc_  = false;
    bool has_mips4_ = false;   /* MIPS IV integer ops (MOVZ/MOVN/PREF) present */
    bool has_vr41xx_power_modes_ = false;   /* STANDBY/SUSPEND/HIBERNATE present */
    bool has_64bit_ = false;   /* doubleword ops + DMFC0/DMTC0 (MIPS III and up) */
    bool has_eret_  = false;   /* ERET / WAIT present */
    bool has_rfe_   = false;   /* RFE present (MIPS I; ERET's predecessor) */
    bool has_mips16_ = false;  /* MIPS16 ASE enabled; gates JALX (U15509EJ2V0UM 3.4.3) */
};
