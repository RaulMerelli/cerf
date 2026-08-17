#include "thumb32_decoder.h"

#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

namespace {

void SetSingleImm(DecodedInsn* d, uint32_t op, bool load, bool byte,
                  uint32_t imm, bool p = true, bool u = true,
                  bool w = false) {
    d->thumb_encoding = true;
    d->p = p;
    d->u = u;
    d->w = w;
    d->l = load;
    d->s = byte;
    d->n = 1u;
    d->rn = (op >> 16) & 0xFu;
    d->rd = (op >> 12) & 0xFu;
    d->offset = u ? static_cast<int32_t>(imm) : -static_cast<int32_t>(imm);
    d->r15_modified = load && !byte && d->rd == ArmGpr::kR15;
    d->place_fn = &PlaceSingleDataTransfer;
}

void SetSingleReg(DecodedInsn* d, uint32_t op, bool load, bool byte) {
    d->thumb_encoding = true;
    d->p = 1u;
    d->u = 1u;
    d->l = load;
    d->s = byte;
    d->n = 0u;
    d->rn = (op >> 16) & 0xFu;
    d->rd = (op >> 12) & 0xFu;
    d->rm = op & 0xFu;
    d->op1 = kSrLsl;
    d->rs = (op >> 4) & 3u;
    d->r15_modified = load && !byte && d->rd == ArmGpr::kR15;
    d->place_fn = &PlaceSingleDataTransfer;
}

void SetExtension(DecodedInsn* d, uint32_t op, uint32_t kind, bool load,
                  bool immediate, uint32_t amount, bool p = true,
                  bool u = true, bool w = false) {
    d->thumb_encoding = true;
    d->op1 = kind;
    d->p = p;
    d->u = u;
    d->w = w;
    d->l = load;
    d->n = immediate;
    d->rn = (op >> 16) & 0xFu;
    d->rd = (op >> 12) & 0xFu;
    if (immediate) {
        d->offset = u ? static_cast<int32_t>(amount)
                      : -static_cast<int32_t>(amount);
    } else {
        d->rm = op & 0xFu;
        d->rs = amount;
    }
    d->place_fn = &PlaceLoadStoreExtension;
}

bool IsRegForm(uint32_t op) {
    return (op & 0x0FC0u) == 0u;
}

}  /* namespace */

/* ARM DDI 0406C.c Table A6-23 (p. A6-242). */
bool Thumb32Decoder::DecodeStoreSingleDataItem(DecodedInsn* insn,
                                               uint32_t op) {
    const uint32_t head = op & 0xFFF00000u;
    if (head == 0xF8C00000u || head == 0xF8800000u ||
        head == 0xF8A00000u) {
        if (head == 0xF8A00000u) {
            SetExtension(insn, op, 1u, false, true, op & 0xFFFu);
        } else {
            SetSingleImm(insn, op, false, head == 0xF8800000u,
                         op & 0xFFFu);
        }
        return true;
    }
    if (head == 0xF8400000u || head == 0xF8000000u ||
        head == 0xF8200000u) {
        if ((op & 0x0800u) != 0u) {
            const bool p = ((op >> 10) & 1u) != 0u;
            const bool u = ((op >> 9) & 1u) != 0u;
            const bool w = ((op >> 8) & 1u) != 0u;
            if (head == 0xF8200000u) {
                SetExtension(insn, op, 1u, false, true, op & 0xFFu, p, u, w);
            } else {
                SetSingleImm(insn, op, false, head == 0xF8000000u,
                             op & 0xFFu, p, u, w);
            }
            return true;
        }
        if (IsRegForm(op)) {
            if (head == 0xF8200000u) {
                SetExtension(insn, op, 1u, false, false,
                             (op >> 4) & 3u);
            } else {
                SetSingleReg(insn, op, false, head == 0xF8000000u);
            }
            return true;
        }
    }
    Unimplemented("store single data item (A6-242)", insn, op);
}

/* ARM DDI 0406C.c Table A6-22 (p. A6-241): LDRB/LDRSB and hints. */
bool Thumb32Decoder::DecodeLoadByteMemoryHints(DecodedInsn* insn,
                                               uint32_t op) {
    const uint32_t head = op & 0xFFF00000u;
    const bool hint = ((op >> 12) & 0xFu) == ArmGpr::kR15;
    if (hint) {
        insn->place_fn = &PlaceNop;
        return true;
    }
    if (head == 0xF8900000u || head == 0xF9900000u) {
        if (head == 0xF9900000u) {
            SetExtension(insn, op, 2u, true, true, op & 0xFFFu);
        } else {
            SetSingleImm(insn, op, true, true, op & 0xFFFu);
            if (insn->rn == ArmGpr::kR15) {
                /* ARM DDI 0406C.c A8.8.66 LDRB (literal), encoding T1:
                   the literal base is Align(PC, 4), as in the modified
                   6.6 decoder's synthesized ARM transfer. */
                insn->offset -= static_cast<int32_t>(
                    (insn->guest_address + 4u) & 3u);
            }
        }
        return true;
    }
    if (head == 0xF8100000u || head == 0xF9100000u) {
        if ((op & 0x0800u) != 0u) {
            const bool p = ((op >> 10) & 1u) != 0u;
            const bool u = ((op >> 9) & 1u) != 0u;
            const bool w = ((op >> 8) & 1u) != 0u;
            if (head == 0xF9100000u) {
                SetExtension(insn, op, 2u, true, true, op & 0xFFu, p, u, w);
            } else {
                SetSingleImm(insn, op, true, true, op & 0xFFu, p, u, w);
            }
            return true;
        }
        if (IsRegForm(op)) {
            if (head == 0xF9100000u) {
                SetExtension(insn, op, 2u, true, false, (op >> 4) & 3u);
            } else {
                SetSingleReg(insn, op, true, true);
            }
            return true;
        }
    }
    Unimplemented("load byte, memory hints (A6-241)", insn, op);
}

/* ARM DDI 0406C.c Table A6-21 (p. A6-240): LDRH/LDRSH and hints. */
bool Thumb32Decoder::DecodeLoadHalfwordMemoryHints(DecodedInsn* insn,
                                                   uint32_t op) {
    const uint32_t head = op & 0xFFF00000u;
    if (((op >> 12) & 0xFu) == ArmGpr::kR15) {
        insn->place_fn = &PlaceNop;
        return true;
    }
    if (head == 0xF8B00000u || head == 0xF9B00000u) {
        SetExtension(insn, op, head == 0xF8B00000u ? 1u : 3u,
                     true, true, op & 0xFFFu);
        return true;
    }
    if (head == 0xF8300000u || head == 0xF9300000u) {
        const uint32_t kind = head == 0xF8300000u ? 1u : 3u;
        if ((op & 0x0800u) != 0u) {
            SetExtension(insn, op, kind, true, true, op & 0xFFu,
                         ((op >> 10) & 1u) != 0u,
                         ((op >> 9) & 1u) != 0u,
                         ((op >> 8) & 1u) != 0u);
            return true;
        }
        if (IsRegForm(op)) {
            SetExtension(insn, op, kind, true, false, (op >> 4) & 3u);
            return true;
        }
    }
    Unimplemented("load halfword, memory hints (A6-240)", insn, op);
}

/* ARM DDI 0406C.c Table A6-20 (p. A6-239): LDR word. */
bool Thumb32Decoder::DecodeLoadWord(DecodedInsn* insn, uint32_t op) {
    const uint32_t head = op & 0xFFF00000u;
    if (head == 0xF8D00000u) {
        SetSingleImm(insn, op, true, false, op & 0xFFFu);
        if (insn->rn == ArmGpr::kR15) {
            insn->offset -= static_cast<int32_t>((insn->guest_address + 4u) & 3u);
        }
        return true;
    }
    if (head == 0xF8500000u) {
        if ((op & 0x0800u) != 0u) {
            SetSingleImm(insn, op, true, false, op & 0xFFu,
                         ((op >> 10) & 1u) != 0u,
                         ((op >> 9) & 1u) != 0u,
                         ((op >> 8) & 1u) != 0u);
            return true;
        }
        if (IsRegForm(op)) {
            SetSingleReg(insn, op, true, false);
            return true;
        }
    }
    Unimplemented("load word (A6-239)", insn, op);
}

/* ARM DDI 0406C.c Table A6-18 (p. A6-238): exclusive, table branch and
   explicit-pair dual transfers implemented by the modified 6.6 decoder. */
bool Thumb32Decoder::DecodeLoadStoreDualExclusiveTableBranch(
        DecodedInsn* insn, uint32_t op) {
    if ((op & 0xFFF0FFF0u) == 0xE8D0F000u ||
        (op & 0xFFF0FFF0u) == 0xE8D0F010u) {
        insn->rn = (op >> 16) & 0xFu;
        insn->rm = op & 0xFu;
        insn->s = (op & 0x10u) != 0u;
        insn->r15_modified = true;
        insn->place_fn = &PlaceTableBranch;
        return true;
    }
    if ((op & 0xFFF00F00u) == 0xE8500F00u) {
        insn->rn = (op >> 16) & 0xFu;
        insn->rd = (op >> 12) & 0xFu;
        insn->offset = static_cast<int32_t>((op & 0xFFu) << 2);
        insn->place_fn = &PlaceLdrex;
        return true;
    }
    if ((op & 0xFFF00000u) == 0xE8400000u &&
        ((op >> 8) & 0xFu) != ArmGpr::kR15) {
        insn->rn = (op >> 16) & 0xFu;
        insn->rm = (op >> 12) & 0xFu;
        insn->rd = (op >> 8) & 0xFu;
        insn->offset = static_cast<int32_t>((op & 0xFFu) << 2);
        insn->place_fn = &PlaceStrex;
        return true;
    }
    if ((op & 0xFE400000u) == 0xE8400000u) {
        insn->p = (op >> 24) & 1u;
        insn->u = (op >> 23) & 1u;
        insn->w = (op >> 21) & 1u;
        insn->l = (op >> 20) & 1u;
        insn->rn = (op >> 16) & 0xFu;
        insn->rd = (op >> 12) & 0xFu;
        insn->rs = (op >> 8) & 0xFu;
        insn->offset = static_cast<int32_t>((op & 0xFFu) << 2);
        insn->place_fn = &PlaceThumbDoubleTransfer;
        return true;
    }
    Unimplemented("load/store dual, load/store exclusive, table branch "
                  "(A6-238)", insn, op);
}
