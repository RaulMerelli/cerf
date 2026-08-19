#include "thumb32_load_byte_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb32_fatal.h"

REGISTER_SERVICE(Thumb32LoadByteDecoder);

bool Thumb32LoadByteDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32LoadByteDecoder::OnReady() {
    fatal_ = &emu_.Get<Thumb32Fatal>();
}

/* DDI 0406C.c A8.8.128 PLD, PLDW (register) encoding T1 (p. A8-528) and
   A8.8.130 PLI (register) encoding T1 (p. A8-532): Rm = hw2[3:0], "if m IN
   {13,15} then UNPREDICTABLE". A3.9.4 (p. A3-158): "The Preload instructions
   are hints, and so implementations can treat them as NOPs". */
bool Thumb32LoadByteDecoder::DecodePreloadRegister(DecodedInsn* insn,
                                                   uint32_t op) {
    const uint32_t rm = op & 0xFu;
    if (rm == 13u || rm == 0xFu) {
        return false;
    }
    insn->place_fn = &PlaceNop;
    return true;
}

/* DDI 0406C.c A8.8.84 LDRSB (immediate) encoding T1 (p. A8-450): "imm32 =
   ZeroExtend(imm12, 32); index = TRUE; add = TRUE; wback = FALSE", "if t == 13
   then UNPREDICTABLE"; Rt == '1111' is "SEE PLI" and Rn == '1111' is "SEE
   LDRSB (literal)". */
bool Thumb32LoadByteDecoder::DecodeSignedByteImmediate12(DecodedInsn* insn,
                                                         uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = 2u;
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

/* DDI 0406C.c A8.8.84 LDRSB (immediate) encoding T2 (p. A8-450): hw2[10:8] =
   P:U:W, "imm32 = ZeroExtend(imm8, 32); index = (P == '1'); add = (U == '1');
   wback = (W == '1')", "if t == 13 || (t == 15 && W == '1') || (wback &&
   n == t) then UNPREDICTABLE". */
bool Thumb32LoadByteDecoder::DecodeSignedByteImmediate8(DecodedInsn* insn,
                                                        uint32_t op) {
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
    insn->op1      = 2u;
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

/* DDI 0406C.c A8.8.87 LDRSBT encoding T1 (p. A8-456): "postindex = FALSE;
   add = TRUE", "if t IN {13,15} then UNPREDICTABLE"; the Thumb form "uses an
   offset addressing mode ... and leaves the base register unchanged". */
bool Thumb32LoadByteDecoder::DecodeSignedByteUnprivileged(DecodedInsn* insn,
                                                          uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = 2u;
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

/* DDI 0406C.c A6.3.9 Table A6-20 (p. A6-241): op1 = bits[24:23], Rn =
   bits[19:16], Rt = hw2[15:12], op2 = hw2[11:6]; "Other encodings in this
   space are UNDEFINED". A3.9.4 (p. A3-158): "The Preload instructions are
   hints, and so implementations can treat them as NOPs". */
bool Thumb32LoadByteDecoder::DecodeLoadByteMemoryHints(DecodedInsn* insn,
                                                       uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rt  = (op >> 12) & 0xFu;
    const uint32_t op2 = (op >>  6) & 0x3Fu;
    const bool     signed_byte = op1 >= 0x2u;

    if (rn == 0xFu) {
        if (rt == 0xFu) {
            insn->place_fn = &PlaceNop;
            return true;
        }
        fatal_->Unimplemented(signed_byte
                                  ? "load register signed byte, literal "
                                    "(A6-241)"
                                  : "load register byte, literal (A6-241)",
                              insn, op);
    }

    if (op1 == 0x1u || op1 == 0x3u) {
        if (rt == 0xFu) {
            insn->place_fn = &PlaceNop;
            return true;
        }
        if (signed_byte) {
            return DecodeSignedByteImmediate12(insn, op);
        }
        fatal_->Unimplemented("load register byte, immediate (A6-241)", insn,
                              op);
    }

    if (op2 == 0x00u) {
        if (rt == 0xFu) {
            return DecodePreloadRegister(insn, op);
        }
        fatal_->Unimplemented(signed_byte
                                  ? "load register signed byte, register "
                                    "(A6-241)"
                                  : "load register byte, register (A6-241)",
                              insn, op);
    }
    if ((op2 & 0x24u) == 0x24u || (op2 & 0x3Cu) == 0x30u) {
        if (rt == 0xFu && (op2 & 0x3Cu) == 0x30u) {
            insn->place_fn = &PlaceNop;
            return true;
        }
        if (signed_byte) {
            return DecodeSignedByteImmediate8(insn, op);
        }
        fatal_->Unimplemented("load register byte, immediate (A6-241)", insn,
                              op);
    }
    if ((op2 & 0x3Cu) == 0x38u) {
        if (signed_byte) {
            return DecodeSignedByteUnprivileged(insn, op);
        }
        fatal_->Unimplemented("load register byte unprivileged (A6-241)", insn,
                              op);
    }
    return false;
}
