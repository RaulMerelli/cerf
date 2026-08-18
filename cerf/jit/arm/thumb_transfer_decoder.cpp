#include "thumb_transfer_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ThumbTransferDecoder);

bool ThumbTransferDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

/* ARM DDI 0100I A7.1.30 LDR (3), p. A7-51. */
bool ThumbTransferDecoder::DecodeLoadLiteral(DecodedInsn* insn, uint16_t op) {
    insn->p        = 1u;
    insn->u        = 1u;
    insn->s        = 0u;
    insn->w        = 0u;
    insn->l        = 1u;
    insn->n        = 1u;
    insn->rn       = 15u;
    insn->rd       = (op >> 8) & 0x7u;
    insn->offset   = static_cast<int32_t>((op & 0xFFu) * 4u);
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* ARM DDI 0100I A7.1.59 STR (2) (p. A7-101), A7.1.64 STRH (2) (p. A7-111),
   A7.1.62 STRB (2) (p. A7-107), A7.1.36 LDRSB (p. A7-61), A7.1.29 LDR (2)
   (p. A7-49), A7.1.35 LDRH (2) (p. A7-59), A7.1.33 LDRB (2) (p. A7-56),
   A7.1.37 LDRSH (p. A7-62). */
bool ThumbTransferDecoder::DecodeRegisterOffsetTransfer(DecodedInsn* insn,
                                                       uint16_t     op) {
    insn->p  = 1u;
    insn->u  = 1u;
    insn->w  = 0u;
    insn->n  = 0u;
    insn->rm = (op >> 6) & 0x7u;
    insn->rn = (op >> 3) & 0x7u;
    insn->rd =  op       & 0x7u;
    switch ((op >> 9) & 0x7u) {
    case 0u:
        insn->s = 0u;
        insn->l = 0u;
        break;
    case 2u:
        insn->s = 1u;
        insn->l = 0u;
        break;
    case 4u:
        insn->s = 0u;
        insn->l = 1u;
        break;
    case 6u:
        insn->s = 1u;
        insn->l = 1u;
        break;
    case 1u:
        insn->op1      = 1u;
        insn->l        = 0u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    case 5u:
        insn->op1      = 1u;
        insn->l        = 1u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    case 3u:
        insn->op1      = 2u;
        insn->l        = 1u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    default:
        insn->op1      = 3u;
        insn->l        = 1u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    }
    insn->op1      = kSrLsl;
    insn->rs       = 0u;
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* ARM DDI 0100I A7.1.58 STR (1) (p. A7-99), A7.1.28 LDR (1) (p. A7-47),
   A7.1.61 STRB (1) (p. A7-105), A7.1.32 LDRB (1) (p. A7-55). */
bool ThumbTransferDecoder::DecodeImmediateOffsetTransfer(DecodedInsn* insn,
                                                         uint16_t     op) {
    const uint32_t byte    = (op >> 12) & 0x1u;
    const uint32_t immed_5 = (op >>  6) & 0x1Fu;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->s        = byte;
    insn->w        = 0u;
    insn->l        = (op >> 11) & 0x1u;
    insn->n        = 1u;
    insn->rn       = (op >> 3) & 0x7u;
    insn->rd       =  op       & 0x7u;
    insn->offset   =
        static_cast<int32_t>(byte != 0u ? immed_5 : immed_5 * 4u);
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* ARM DDI 0100I A7.1.63 STRH (1) (p. A7-109), A7.1.34 LDRH (1) (p. A7-57). */
bool ThumbTransferDecoder::DecodeHalfwordOffsetTransfer(DecodedInsn* insn,
                                                        uint16_t     op) {
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->n        = 1u;
    insn->op1      = 1u;
    insn->l        = (op >> 11) & 0x1u;
    insn->rn       = (op >> 3) & 0x7u;
    insn->rd       =  op       & 0x7u;
    insn->offset   = static_cast<int32_t>(((op >> 6) & 0x1Fu) * 2u);
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* ARM DDI 0100I A7.1.60 STR (3) (p. A7-103), A7.1.31 LDR (4) (p. A7-53). */
bool ThumbTransferDecoder::DecodeStackRelativeTransfer(DecodedInsn* insn,
                                                       uint16_t     op) {
    insn->p        = 1u;
    insn->u        = 1u;
    insn->s        = 0u;
    insn->w        = 0u;
    insn->l        = (op >> 11) & 0x1u;
    insn->n        = 1u;
    insn->rn       = 13u;
    insn->rd       = (op >> 8) & 0x7u;
    insn->offset   = static_cast<int32_t>((op & 0xFFu) * 4u);
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* ARM DDI 0406C.c A8.8.57 LDM/LDMIA/LDMFD (Thumb) T1 (p. A8-396): "wback =
   (registers<n> == '0')"; A8.8.199 STM (STMIA, STMEA) T1 (p. A8-664): "wback
   = TRUE". Both: "if BitCount(registers) < 1 then UNPREDICTABLE". */
bool ThumbTransferDecoder::DecodeLoadStoreMultiple(DecodedInsn* insn,
                                                   uint16_t     op) {
    const uint32_t rn   = (op >> 8) & 0x7u;
    const uint32_t list =  op       & 0xFFu;
    if (list == 0u) {
        return false;
    }
    const bool load = ((op >> 11) & 0x1u) != 0u;
    insn->register_list = static_cast<uint16_t>(list);
    insn->rn            = rn;
    insn->l             = load ? 1u : 0u;
    insn->p             = 0u;
    insn->u             = 1u;
    insn->s             = 0u;
    insn->w             = (load && ((list >> rn) & 0x1u) != 0u) ? 0u : 1u;
    insn->place_fn      = &PlaceBlockDataTransfer;
    return true;
}
