#include "thumb32_load_halfword_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(Thumb32LoadHalfwordDecoder);

bool Thumb32LoadHalfwordDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

/* DDI 0406C.c A8.8.81 LDRH (literal) encoding T1 (p. A8-444) and A8.8.89 LDRSH
   (literal) encoding T1 (p. A8-460): U = bit[23], "imm32 = ZeroExtend(imm12,
   32); add = (U == '1')", "if t == 13 then UNPREDICTABLE". */
bool Thumb32LoadHalfwordDecoder::DecodeLiteral(DecodedInsn* insn, uint32_t op,
                                               uint32_t ext_op) {
    const uint32_t add   = (op >> 23) & 0x1u;
    const uint32_t rt    = (op >> 12) & 0xFu;
    const uint32_t imm12 =  op        & 0xFFFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = add;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = 15u;
    insn->rd       = rt;
    insn->offset   = add != 0u ? static_cast<int32_t>(imm12)
                               : -static_cast<int32_t>(imm12);
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* DDI 0406C.c A8.8.79 LDRH (immediate, Thumb) encoding T2 (p. A8-440) and
   A8.8.88 LDRSH (immediate) encoding T1 (p. A8-458): "imm32 =
   ZeroExtend(imm12, 32); index = TRUE; add = TRUE; wback = FALSE", "if t == 13
   then UNPREDICTABLE". */
bool Thumb32LoadHalfwordDecoder::DecodeImmediate12(DecodedInsn* insn,
                                                   uint32_t op,
                                                   uint32_t ext_op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFFu);
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* DDI 0406C.c A8.8.79 LDRH (immediate, Thumb) T3 (p. A8-440) and A8.8.88
   LDRSH (immediate) T2 (p. A8-458): hw2[10:8] = P:U:W; "index = (P == '1');
   add = (U == '1'); wback = (W == '1')", "if t == 13 || (t == 15 && W == '1')
   || (wback && n == t) then UNPREDICTABLE". */
bool Thumb32LoadHalfwordDecoder::DecodeImmediate8(DecodedInsn* insn,
                                                  uint32_t op,
                                                  uint32_t ext_op) {
    const uint32_t add  = (op >>  9) & 0x1u;
    const uint32_t w    = (op >>  8) & 0x1u;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;
    if (rt == 13u || (rt == 0xFu && w != 0u) || (w != 0u && rn == rt)) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = (op >> 10) & 0x1u;
    insn->u        = add;
    insn->w        = w;
    insn->unpriv   = 0u;
    insn->rn       = rn;
    insn->rd       = rt;
    insn->offset   = add != 0u ? static_cast<int32_t>(imm8)
                               : -static_cast<int32_t>(imm8);
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* DDI 0406C.c A8.8.82 LDRH (register) encoding T2 (p. A8-446) and A8.8.90
   LDRSH (register) encoding T2 (p. A8-462): imm2 = hw2[5:4], Rm = hw2[3:0],
   "index = TRUE; add = TRUE; wback = FALSE; (shift_t, shift_n) = (SRType_LSL,
   UInt(imm2))", "if t == 13 || m IN {13,15} then UNPREDICTABLE". */
bool Thumb32LoadHalfwordDecoder::DecodeRegister(DecodedInsn* insn, uint32_t op,
                                                uint32_t ext_op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rt == 13u || rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->n        = 0u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->rm       = rm;
    insn->rs       = (op >> 4) & 0x3u;
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* DDI 0406C.c A8.8.83 LDRHT encoding T1 (p. A8-448) and A8.8.91 LDRSHT
   encoding T1 (p. A8-464): "postindex = FALSE; add = TRUE", "if t IN {13,15}
   then UNPREDICTABLE"; the Thumb form "uses an offset addressing mode ... and
   leaves the base register unchanged". */
bool Thumb32LoadHalfwordDecoder::DecodeUnprivileged(DecodedInsn* insn,
                                                    uint32_t op,
                                                    uint32_t ext_op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 1u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFu);
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* DDI 0406C.c A6.3.8 Table A6-19 (p. A6-240): op1 = bits[24:23], Rn =
   bits[19:16], Rt = hw2[15:12], op2 = hw2[11:6]. A3.9.4 (p. A3-158): preload
   "hints ... implementations can treat them as NOPs". Footnote a, PLDW rows:
   "does not include the Multiprocessing Extensions ... treated as NOPs". */
bool Thumb32LoadHalfwordDecoder::DecodeLoadHalfwordMemoryHints(
    DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rt  = (op >> 12) & 0xFu;
    const uint32_t op2 = (op >>  6) & 0x3Fu;
    const uint32_t ext_op = op1 >= 0x2u ? 3u : 1u;

    if (rt == 0xFu &&
        (rn == 0xFu || op1 == 0x1u || op1 == 0x3u || op2 == 0x00u ||
         (op2 & 0x3Cu) == 0x30u)) {
        insn->place_fn = &PlaceNop;
        return true;
    }

    if (rn == 0xFu) {
        return DecodeLiteral(insn, op, ext_op);
    }
    if (op1 == 0x1u || op1 == 0x3u) {
        return DecodeImmediate12(insn, op, ext_op);
    }
    if (op2 == 0x00u) {
        return DecodeRegister(insn, op, ext_op);
    }
    if ((op2 & 0x24u) == 0x24u || (op2 & 0x3Cu) == 0x30u) {
        return DecodeImmediate8(insn, op, ext_op);
    }
    if ((op2 & 0x3Cu) == 0x38u) {
        return DecodeUnprivileged(insn, op, ext_op);
    }
    return false;
}
