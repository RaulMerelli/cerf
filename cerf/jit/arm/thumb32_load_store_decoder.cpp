#include "thumb32_load_store_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb32_fatal.h"

REGISTER_SERVICE(Thumb32LoadStoreDecoder);

bool Thumb32LoadStoreDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32LoadStoreDecoder::OnReady() {
    fatal_ = &emu_.Get<Thumb32Fatal>();
}

/* DDI 0406C.c A8.8.64 LDR (literal) encoding T2 (p. A8-410): U = bit[23],
   Rt = hw2[15:12], imm12 = hw2[11:0], "imm32 = ZeroExtend(imm12, 32);
   add = (U == '1')". */
bool Thumb32LoadStoreDecoder::DecodeLoadLiteral(DecodedInsn* insn,
                                                uint32_t op) {
    const uint32_t add   = (op >> 23) & 0x1u;
    const uint32_t rt    = (op >> 12) & 0xFu;
    const uint32_t imm12 =  op        & 0xFFFu;

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = add;
    insn->w            = 0u;
    insn->unpriv       = 0u;
    insn->rn           = 15u;
    insn->rd           = rt;
    insn->offset       = add != 0u ? static_cast<int32_t>(imm12)
                                   : -static_cast<int32_t>(imm12);
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.62 LDR (immediate, Thumb) encoding T3 (p. A8-406):
   "imm32 = ZeroExtend(imm12, 32); index = TRUE; add = TRUE;
   wback = FALSE". */
bool Thumb32LoadStoreDecoder::DecodeLoadImmediate12(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = 1u;
    insn->w            = 0u;
    insn->unpriv       = 0u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->offset       = static_cast<int32_t>(op & 0xFFFu);
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.62 LDR (immediate, Thumb) encoding T4 (p. A8-406):
   hw2[10:8] = P:U:W, imm8 = hw2[7:0], "imm32 = ZeroExtend(imm8, 32);
   index = (P == '1'); add = (U == '1'); wback = (W == '1')". */
bool Thumb32LoadStoreDecoder::DecodeLoadImmediate8(DecodedInsn* insn,
                                                   uint32_t op) {
    const uint32_t add  = (op >> 9) & 0x1u;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = (op >> 10) & 0x1u;
    insn->u            = add;
    insn->w            = (op >>  8) & 0x1u;
    insn->unpriv       = 0u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->offset       = add != 0u ? static_cast<int32_t>(imm8)
                                   : -static_cast<int32_t>(imm8);
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.65 LDR (register, Thumb) encoding T2 (p. A8-412):
   imm2 = hw2[5:4], Rm = hw2[3:0], "(shift_t, shift_n) = (SRType_LSL,
   UInt(imm2)); if m IN {13,15} then UNPREDICTABLE"; the Thumb form does
   not support register writeback. */
bool Thumb32LoadStoreDecoder::DecodeLoadRegister(DecodedInsn* insn,
                                                 uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->n            = 0u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = 1u;
    insn->w            = 0u;
    insn->unpriv       = 0u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->rm           = rm;
    insn->op1          = kSrLsl;
    insn->rs           = (op >> 4) & 0x3u;
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.92 LDRT encoding T1 (p. A8-466): "postindex = FALSE;
   add = TRUE; register_form = FALSE; imm32 = ZeroExtend(imm8, 32);
   if t IN {13,15} then UNPREDICTABLE"; the Thumb form uses an offset
   addressing mode and leaves the base register unchanged. */
bool Thumb32LoadStoreDecoder::DecodeLoadUnprivileged(DecodedInsn* insn,
                                                     uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = 1u;
    insn->w            = 0u;
    insn->unpriv       = 1u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->offset       = static_cast<int32_t>(op & 0xFFu);
    insn->r15_modified = false;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A6.3.7 and Table A6-18 p. A6-239: op1 = bits[24:23],
   op2 = hw2[11:6], Rn = bits[19:16]; other encodings in this space are
   UNDEFINED. The SEE redirects are A8-406/410/412/466 encoding line 1. */
bool Thumb32LoadStoreDecoder::DecodeLoadWord(DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    if (op1 > 0x1u) {
        return false;
    }
    if (((op >> 16) & 0xFu) == 0xFu) {
        return DecodeLoadLiteral(insn, op);
    }
    if (op1 == 0x1u) {
        return DecodeLoadImmediate12(insn, op);
    }

    const uint32_t op2 = (op >> 6) & 0x3Fu;
    if (op2 == 0x00u) {
        return DecodeLoadRegister(insn, op);
    }
    if ((op2 & 0x20u) == 0u) {
        return false;
    }

    const uint32_t p = (op >> 10) & 0x1u;
    const uint32_t u = (op >>  9) & 0x1u;
    const uint32_t w = (op >>  8) & 0x1u;
    if (p != 0u && u != 0u && w == 0u) {
        return DecodeLoadUnprivileged(insn, op);
    }
    /* A8.8.62 encoding T4 (p. A8-406): "if P == '0' && W == '0' then
       UNDEFINED". */
    if (p == 0u && w == 0u) {
        return false;
    }
    return DecodeLoadImmediate8(insn, op);
}

/* DDI 0406C.c A8.8.203 STR (immediate, Thumb) encoding T3 (p. A8-672):
   "imm32 = ZeroExtend(imm12, 32); index = TRUE; add = TRUE; wback = FALSE",
   "if t == 15 then UNPREDICTABLE". */
bool Thumb32LoadStoreDecoder::DecodeStoreImmediate12(DecodedInsn* insn,
                                                     uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 0xFu) {
        return false;
    }

    insn->n        = 1u;
    insn->s        = 0u;
    insn->l        = 0u;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFFu);
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.203 STR (immediate, Thumb) encoding T4 (p. A8-672):
   hw2[10:8] = P:U:W, imm8 = hw2[7:0], "index = (P == '1'); add = (U == '1');
   wback = (W == '1')", "if t == 15 || (wback && n == t) then
   UNPREDICTABLE". */
bool Thumb32LoadStoreDecoder::DecodeStoreImmediate8(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t add  = (op >>  9) & 0x1u;
    const uint32_t w    = (op >>  8) & 0x1u;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;
    if (rt == 0xFu || (w != 0u && rn == rt)) {
        return false;
    }

    insn->n        = 1u;
    insn->s        = 0u;
    insn->l        = 0u;
    insn->p        = (op >> 10) & 0x1u;
    insn->u        = add;
    insn->w        = w;
    insn->unpriv   = 0u;
    insn->rn       = rn;
    insn->rd       = rt;
    insn->offset   = add != 0u ? static_cast<int32_t>(imm8)
                               : -static_cast<int32_t>(imm8);
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.205 STR (register) encoding T2 (p. A8-676): imm2 =
   hw2[5:4], Rm = hw2[3:0], "(shift_t, shift_n) = (SRType_LSL, UInt(imm2))",
   "if t == 15 || m IN {13,15} then UNPREDICTABLE". */
bool Thumb32LoadStoreDecoder::DecodeStoreRegister(DecodedInsn* insn,
                                                  uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rt == 0xFu || rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->n        = 0u;
    insn->s        = 0u;
    insn->l        = 0u;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->rm       = rm;
    insn->op1      = kSrLsl;
    insn->rs       = (op >> 4) & 0x3u;
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.220 STRT encoding T1 (p. A8-706): "postindex = FALSE;
   add = TRUE; register_form = FALSE; imm32 = ZeroExtend(imm8, 32)", "if t IN
   {13,15} then UNPREDICTABLE"; the Thumb form "uses an offset addressing
   mode ... and leaves the base register unchanged". */
bool Thumb32LoadStoreDecoder::DecodeStoreUnprivileged(DecodedInsn* insn,
                                                      uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n        = 1u;
    insn->s        = 0u;
    insn->l        = 0u;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 1u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFu);
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A6.3.10 and Table A6-21 p. A6-242: op1 = bits[23:21],
   op2 = hw2[11:6]; "Other encodings in this space are UNDEFINED". Every word
   row carries "if Rn == '1111' then UNDEFINED" (pp. A8-672/676/706). */
bool Thumb32LoadStoreDecoder::DecodeStoreSingleDataItem(DecodedInsn* insn,
                                                        uint32_t op) {
    const uint32_t op1 = (op >> 21) & 0x7u;
    if (op1 == 0x0u || op1 == 0x4u) {
        fatal_->Unimplemented("store register byte (A6-242)", insn, op);
    }
    if (op1 == 0x1u || op1 == 0x5u) {
        fatal_->Unimplemented("store register halfword (A6-242)", insn, op);
    }
    if (op1 != 0x2u && op1 != 0x6u) {
        return false;
    }
    if (((op >> 16) & 0xFu) == 0xFu) {
        return false;
    }
    if (op1 == 0x6u) {
        return DecodeStoreImmediate12(insn, op);
    }

    const uint32_t op2 = (op >> 6) & 0x3Fu;
    if (op2 == 0x00u) {
        return DecodeStoreRegister(insn, op);
    }
    if ((op2 & 0x20u) == 0u) {
        return false;
    }

    const uint32_t p = (op >> 10) & 0x1u;
    const uint32_t u = (op >>  9) & 0x1u;
    const uint32_t w = (op >>  8) & 0x1u;
    if (p != 0u && u != 0u && w == 0u) {
        return DecodeStoreUnprivileged(insn, op);
    }
    /* A8.8.203 encoding T4 (p. A8-672): "if Rn == '1111' || (P == '0' &&
       W == '0') then UNDEFINED". */
    if (p == 0u && w == 0u) {
        return false;
    }
    return DecodeStoreImmediate8(insn, op);
}

/* DDI 0406C.c A8.8.128 PLD, PLDW (register) encoding T1 (p. A8-528) and
   A8.8.130 PLI (register) encoding T1 (p. A8-532): Rm = hw2[3:0], "if m IN
   {13,15} then UNPREDICTABLE". A3.9.4 (p. A3-158): "The Preload instructions
   are hints, and so implementations can treat them as NOPs". */
bool Thumb32LoadStoreDecoder::DecodePreloadRegister(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rm = op & 0xFu;
    if (rm == 13u || rm == 0xFu) {
        return false;
    }
    insn->place_fn = &PlaceNop;
    return true;
}

/* DDI 0406C.c A6.3.9 Table A6-20 (p. A6-241): op1 = bits[24:23], Rn =
   bits[19:16], Rt = hw2[15:12], op2 = hw2[11:6]; "Other encodings in this
   space are UNDEFINED". A3.9.4 (p. A3-158): "The Preload instructions are
   hints, and so implementations can treat them as NOPs". */
bool Thumb32LoadStoreDecoder::DecodeLoadByteMemoryHints(DecodedInsn* insn,
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
        fatal_->Unimplemented(signed_byte
                                  ? "load register signed byte, immediate "
                                    "(A6-241)"
                                  : "load register byte, immediate (A6-241)",
                              insn, op);
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
        fatal_->Unimplemented(signed_byte
                                  ? "load register signed byte, immediate "
                                    "(A6-241)"
                                  : "load register byte, immediate (A6-241)",
                              insn, op);
    }
    if ((op2 & 0x3Cu) == 0x38u) {
        fatal_->Unimplemented(signed_byte
                                  ? "load register signed byte unprivileged "
                                    "(A6-241)"
                                  : "load register byte unprivileged "
                                    "(A6-241)",
                              insn, op);
    }
    return false;
}
