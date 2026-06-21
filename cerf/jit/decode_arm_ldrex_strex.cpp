#include "arm_decoder.h"

#include "../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

bool ArmDecoder::DecodeArmLdrexStrex(DecodedInsn* insn, ArmOpcode op) {
    if (!processor_config_->HasLdrexStrex()) {
        return false;
    }

    const uint32_t bits27_23 = (op.word >> 23) & 0x1Fu;
    const uint32_t bits22_21 = (op.word >> 21) & 0x3u;
    const uint32_t l         = (op.word >> 20) & 0x1u;
    const uint32_t bits11_8  = (op.word >>  8) & 0xFu;
    const uint32_t bits7_4   = (op.word >>  4) & 0xFu;

    if (bits27_23 != 0b00011u || bits11_8 != 0xFu || bits7_4 != 0x9u) {
        return false;
    }
    if (bits22_21 != 0u) {
        return false;  /* size != word - halt-on-unknown */
    }

    const uint32_t rn = (op.word >> 16) & 0xFu;
    const uint32_t rd = (op.word >> 12) & 0xFu;
    const uint32_t rt =  op.word        & 0xFu;

    /* R15 in Rd or Rn is UNPREDICTABLE per DDI 0100I line 8158
       (§ LDREX "Use of R15"): "If register 15 is specified for
       <Rd> or <Rn>, the result is UNPREDICTABLE." */
    if (rn == ArmGpr::kR15 || rd == ArmGpr::kR15) {
        return false;
    }

    if (l == 1u) {
        /* LDREX: bits[3:0] is SBO=0xF. */
        if (rt != 0xFu) {
            return false;
        }
        insn->rd       = rd;   /* Rt destination */
        insn->rn       = rn;   /* address */
        insn->place_fn = &PlaceLdrex;
        return true;
    }

    /* STREX: bits[3:0] is Rt (source). */
    if (rt == ArmGpr::kR15) {
        return false;
    }
    if (rd == rt || rd == rn) {
        return false;
    }
    insn->rd       = rd;       /* status output (0 success, 1 fail) */
    insn->rn       = rn;       /* address */
    insn->rm       = rt;       /* source value */
    insn->place_fn = &PlaceStrex;
    return true;
}
