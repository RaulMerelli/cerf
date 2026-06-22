#include "arm_decoder.h"

#include "../../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

bool ArmDecoder::DecodeArmClass3Misc(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t bits27_20 = (op.word >> 20) & 0xFFu;
    const uint32_t bits19_16 = (op.word >> 16) & 0x0Fu;
    const uint32_t bits11_8  = (op.word >>  8) & 0x0Fu;
    const uint32_t bits7_4   = (op.word >>  4) & 0x0Fu;

    if (bits19_16 != 0xFu) {
        return false;
    }

    const uint32_t rd = (op.word >> 12) & 0xFu;
    const uint32_t rm =  op.word        & 0xFu;
    if (rd == ArmGpr::kR15 || rm == ArmGpr::kR15) {
        return false;
    }
    insn->rd = rd;
    insn->rm = rm;

    /* REV family. */
    if (bits11_8 == 0xFu && processor_config_->HasRev()) {
        if (bits27_20 == 0x6Bu && bits7_4 == 0x3u) {
            insn->place_fn = &PlaceRev;
            return true;
        }
        if (bits27_20 == 0x6Bu && bits7_4 == 0xBu) {
            insn->place_fn = &PlaceRev16;
            return true;
        }
        if (bits27_20 == 0x6Fu && bits7_4 == 0xBu) {
            insn->place_fn = &PlaceRevsh;
            return true;
        }
        /* No match in the REV family - return false so the caller
           UND-faults (halt-on-unknown). RBIT (0x6F, 0x3) lands here. */
        return false;
    }

    /* SXT/UXT family. bits[9:8] = 00 + bits[7:4] = 0111. */
    if ((bits11_8 & 0x3u) == 0u && bits7_4 == 0x7u &&
        processor_config_->HasExtendRotate()) {
        /* bits[11:8] = rot << 2 | 0. The rot field is bits[11:10]. */
        insn->op1 = (bits11_8 >> 2) & 0x3u;  /* rot ∈ {0,1,2,3} */
        switch (bits27_20) {
        case 0x6Au: insn->place_fn = &PlaceSxtb; return true;
        case 0x6Bu: insn->place_fn = &PlaceSxth; return true;
        case 0x6Eu: insn->place_fn = &PlaceUxtb; return true;
        case 0x6Fu: insn->place_fn = &PlaceUxth; return true;
        default: break;
        }
    }

    return false;
}
