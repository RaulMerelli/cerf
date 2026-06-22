#include "arm_decoder.h"

#include "../../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

bool ArmDecoder::DecodeArmBitfield(DecodedInsn* insn, ArmOpcode op) {
    if (!processor_config_->HasBitField()) {
        return false;
    }

    const uint32_t bits27_21 = (op.word >> 21) & 0x7Fu;
    const uint32_t bits6_4   = (op.word >>  4) & 0x07u;

    const uint32_t rd        = (op.word >> 12) & 0xFu;
    const uint32_t rn        =  op.word        & 0xFu;
    const uint32_t lsb       = (op.word >>  7) & 0x1Fu;
    const uint32_t field_bits16_20 = (op.word >> 16) & 0x1Fu;

    if (bits27_21 == 0x3Eu && bits6_4 == 0x1u) {
        /* BFI (Rn != 15) or BFC (Rn == 15). bits[20:16] encode msb. */
        const uint32_t msb   = field_bits16_20;
        if (msb < lsb || rd == ArmGpr::kR15) {
            return false;  /* UNPREDICTABLE encodings */
        }
        const uint32_t width = msb - lsb + 1u;
        const uint32_t mask  = (width == 32u)
            ? 0xFFFFFFFFu
            : (((1u << width) - 1u) << lsb);
        insn->rd        = rd;
        insn->rn        = rn;
        insn->op1       = lsb;
        insn->rs        = width;
        insn->immediate = mask;
        insn->place_fn  = (rn == 15u) ? &PlaceBfc : &PlaceBfi;
        return true;
    }

    if ((bits27_21 == 0x3Du || bits27_21 == 0x3Fu) && bits6_4 == 0x5u) {
        /* SBFX (0x3D) or UBFX (0x3F). bits[20:16] encode width-1. */
        const uint32_t width = field_bits16_20 + 1u;
        if (lsb + width > 32u || rd == ArmGpr::kR15 || rn == ArmGpr::kR15) {
            return false;
        }
        insn->rd        = rd;
        insn->rn        = rn;
        insn->op1       = lsb;
        insn->rs        = width;
        insn->place_fn  = (bits27_21 == 0x3Du) ? &PlaceSbfx : &PlaceUbfx;
        return true;
    }

    return false;
}
