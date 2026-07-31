#include "arm_decoder.h"

#include "../../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "neon_unconditional_decoder.h"
#include "place_fns.h"

bool ArmDecoder::DecodeArmUnconditional(DecodedInsn* insn, ArmOpcode op) {
    /* RFE - Return From Exception. ddi0406c B9.3.13 encoding A1:
       1111 100P U0W1 nnnn 0000 1010 0000 0000.
       Extract P (bit 24), U (bit 23), W (bit 21), Rn (bits 19:16). */
    if ((op.word & 0xFE50FFFFu) == 0xF8100A00u) {
        const uint32_t rn = (op.word >> 16) & 0xFu;
        if (rn == ArmGpr::kR15) {
            return false;  /* UNPREDICTABLE per spec - fall through to UND. */
        }
        insn->place_fn            = &PlaceRfe;
        insn->cond                = 14;
        insn->p                   = (op.word >> 24) & 0x1u;
        insn->u                   = (op.word >> 23) & 0x1u;
        insn->w                   = (op.word >> 21) & 0x1u;
        insn->rn                  = rn;
        insn->r15_modified        = true;
        insn->is_exception_return = 1;
        return true;
    }

    /* SRS - Store Return State. ddi0406c B9.3.16 encoding A1:
       1111 100P U1W0 1101 0000 0101 000 mode[4:0].
       Extract P (bit 24), U (bit 23), W (bit 21), target_mode (bits 4:0). */
    if ((op.word & 0xFE5FFFE0u) == 0xF84D0500u) {
        insn->place_fn  = &PlaceSrs;
        insn->cond      = 14;
        insn->p         = (op.word >> 24) & 0x1u;
        insn->u         = (op.word >> 23) & 0x1u;
        insn->w         = (op.word >> 21) & 0x1u;
        insn->immediate = op.word & 0x1Fu;  /* target_mode */
        return true;
    }

    /* DSB / DMB / ISB, ddi0406c A8.8.44 / A8.8.43 / A8.8.53 encoding A1:
       1111 0101 0111 (1)x4 (1)x4 (0)x4 01xx option, bits [7:4] = 0100 DSB /
       0101 DMB / 0110 ISB. NOP emit on x86's strong memory model. */
    if (processor_config_->HasBarrierInsn() &&
        (op.word & 0xFFFFFF00u) == 0xF57FF000u) {
        const uint32_t barrier_op = (op.word >> 4) & 0xFu;
        if (barrier_op >= 0x4u && barrier_op <= 0x6u) {
            insn->place_fn = &PlaceNop;
            insn->cond     = 14;
            return true;
        }
    }

    if (processor_config_->HasNeon() && op.neon_load_store.marker == 0x4u) {
        return neon_unconditional_decoder_->DecodeLoadStore(insn, op);
    }

    if (processor_config_->HasNeon() && op.neon_data_3reg.marker == 0x1u) {
        return neon_unconditional_decoder_->DecodeData3reg(insn, op);
    }

    return false;
}
