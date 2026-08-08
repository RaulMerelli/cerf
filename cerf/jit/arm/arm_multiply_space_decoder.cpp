#include "arm_multiply_space_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmMultiplySpaceDecoder);

bool ArmMultiplySpaceDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmMultiplySpaceDecoder::OnReady() {
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
}

/* DDI 0406C.c Table A5-7 (p. A5-202): op = insn[23:20]; 0101 / 0111 are
   UNDEFINED, UMAAL is v6, MLS is v6T2. A1 encodings (MUL p. A8-502, MLA
   A8-480, UMAAL A8-774, MLS A8-482, UMULL A8-778, UMLAL A8-776, SMULL
   A8-646, SMLAL A8-624): Rd or RdHi = insn[19:16], Ra or RdLo =
   insn[15:12], Rm = insn[11:8], Rn = insn[3:0]; MUL's insn[15:12] is (0);
   any register 15 UNPREDICTABLE; dHi == dLo UNPREDICTABLE;
   ArchVersion() < 6: d == n (MUL, MLA) and dHi == n / dLo == n (the
   long forms) UNPREDICTABLE. */
bool ArmMultiplySpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t row = (op.word >> 21) & 0x7u;
    const uint32_t s   = (op.word >> 20) & 0x1u;
    const uint32_t rd  = (op.word >> 16) & 0xFu;
    const uint32_t ra  = (op.word >> 12) & 0xFu;
    const uint32_t rm  = (op.word >>  8) & 0xFu;
    const uint32_t rn  =  op.word        & 0xFu;

    if (row == 2u && (s != 0u || !processor_config_->HasCp15V6())) {
        return false;
    }
    if (row == 3u && (s != 0u || !processor_config_->HasMls())) {
        return false;
    }
    if (rd == ArmGpr::kR15 || rm == ArmGpr::kR15 || rn == ArmGpr::kR15) {
        return false;
    }
    if (row == 0u) {
        if (ra != 0u) {
            return false;
        }
    } else if (ra == ArmGpr::kR15) {
        return false;
    }
    const bool is_long = row == 2u || row >= 4u;
    if (is_long && rd == ra) {
        return false;
    }
    if (!processor_config_->HasCp15V6()) {
        if (row <= 1u && rd == rn) {
            return false;
        }
        if (row >= 4u && (rd == rn || ra == rn)) {
            return false;
        }
    }

    insn->op1      = row;
    insn->s        = s;
    insn->rd       = rd;
    insn->rs       = ra;
    insn->rm       = rm;
    insn->rn       = rn;
    insn->place_fn = &PlaceMultiply;
    return true;
}

/* DDI 0406C.c Table A5-9 (p. A5-203), A5.2.7: op1 = insn[22:21], op =
   insn[5]; "available in ARMv5TE and above, and are UNDEFINED in earlier
   variants". A1 encodings (SMLA<x><y> p. A8-620, SMLAW<y> A8-630,
   SMULW<y> A8-648, SMLAL<x><y> A8-626, SMUL<x><y> A8-644): Rd or RdHi =
   insn[19:16], Ra or RdLo = insn[15:12], Rm = insn[11:8], Rn = insn[3:0],
   M = insn[6], N = insn[5]; SMUL / SMULW insn[15:12] is (0); any register
   15 UNPREDICTABLE; SMLAL dHi == dLo UNPREDICTABLE. */
bool ArmMultiplySpaceDecoder::DecodeHalfword(DecodedInsn* insn, ArmOpcode op) {
    if (!processor_config_->HasDsp()) {
        return false;
    }
    const uint32_t row = (op.word >> 21) & 0x3u;
    const uint32_t rd  = (op.word >> 16) & 0xFu;
    const uint32_t ra  = (op.word >> 12) & 0xFu;
    const uint32_t rm  = (op.word >>  8) & 0xFu;
    const uint32_t rn  =  op.word        & 0xFu;
    const uint32_t m   = (op.word >>  6) & 0x1u;
    const uint32_t n   = (op.word >>  5) & 0x1u;

    if (rd == ArmGpr::kR15 || rm == ArmGpr::kR15 || rn == ArmGpr::kR15) {
        return false;
    }
    const bool reads_ra = row == 0u || row == 2u || (row == 1u && n == 0u);
    if (reads_ra) {
        if (ra == ArmGpr::kR15) {
            return false;
        }
        if (row == 2u && rd == ra) {
            return false;
        }
    } else if (ra != 0u) {
        return false;
    }

    insn->op1      = row;
    insn->s        = (row == 1u) ? n : 0u;
    insn->n        = (row == 1u) ? 0u : n;
    insn->u        = m;
    insn->rd       = rd;
    insn->rs       = ra;
    insn->rm       = rm;
    insn->rn       = rn;
    insn->place_fn = &PlaceHalfwordMultiply;
    return true;
}
