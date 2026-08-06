#include "arm_unconditional_space_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "neon_unconditional_decoder.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmUnconditionalSpaceDecoder);

bool ArmUnconditionalSpaceDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmUnconditionalSpaceDecoder::OnReady() {
    processor_config_           = &emu_.Get<ArmProcessorConfig>();
    neon_unconditional_decoder_ = &emu_.Get<NeonUnconditionalDecoder>();
}

bool ArmUnconditionalSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    /* RFE - Return From Exception. ddi0406c B9.3.13 encoding A1:
       1111 100P U0W1 nnnn 0000 1010 0000 0000.
       Extract P (bit 24), U (bit 23), W (bit 21), Rn (bits 19:16).
       Table A5-23 (p. A5-216) marks the RFE and SRS rows v6. */
    if (processor_config_->HasCp15V6() &&
        (op.word & 0xFE50FFFFu) == 0xF8100A00u) {
        const uint32_t rn = (op.word >> 16) & 0xFu;
        if (rn == ArmGpr::kR15) {
            return false;  /* UNPREDICTABLE per spec - fall through to UND. */
        }
        insn->place_fn            = &PlaceRfe;
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
    if (processor_config_->HasCp15V6() &&
        (op.word & 0xFE5FFFE0u) == 0xF84D0500u) {
        insn->place_fn  = &PlaceSrs;
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
            return true;
        }
    }

    if (processor_config_->HasNeon() && op.neon_load_store.marker == 0x4u) {
        return neon_unconditional_decoder_->DecodeLoadStore(insn, op);
    }

    if (processor_config_->HasNeon() && op.neon_data_3reg.marker == 0x1u) {
        return neon_unconditional_decoder_->DecodeData3reg(insn, op);
    }

    /* DDI 0406C.c Table A5-23 (p. A5-216), op1 = insn[27:20]: 0xxxxxxx
       memory hints / Advanced SIMD / misc (p. A5-217), 101xxxxx BL/BLX
       (immediate) (p. A8-348), 110xxxx0 not 11000x00 STC/STC2, 110xxxx1
       not 11000x01 LDC/LDC2, 11000100 MCRR2, 11000101 MRRC2, 1110xxxx
       CDP2/MCR2/MRC2; other encodings are UNDEFINED in ARMv5 and above. */
    const uint32_t op1 = (op.word >> 20) & 0xFFu;
    if ((op1 & 0x80u) == 0x00u ||
        (op1 & 0xE0u) == 0xA0u ||
        ((op1 & 0xE0u) == 0xC0u && op1 != 0xC0u && op1 != 0xC1u) ||
        (op1 & 0xF0u) == 0xE0u) {
        return MarkArmUnimplemented(insn, op.word);
    }
    return false;
}
