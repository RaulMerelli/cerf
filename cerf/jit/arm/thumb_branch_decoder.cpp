#include "thumb_decoder.h"

#include "decoded_insn.h"
#include "place_fns.h"

/* ARM DDI 0100I A7.1.13 B (1), p. A7-19. */
bool ThumbDecoder::DecodeConditionalBranch(DecodedInsn* insn, uint16_t op) {
    const uint32_t imm8 = op & 0xFFu;
    insn->l        = 0u;
    insn->offset   = static_cast<int32_t>(((imm8 ^ 0x80u) - 0x80u) << 1);
    insn->place_fn = &PlaceBranch;
    return true;
}

/* ARM DDI 0100I A7.1.14 B (2), p. A7-21. */
bool ThumbDecoder::DecodeUnconditionalBranch(DecodedInsn* insn, uint16_t op) {
    const uint32_t off11 = op & 0x7FFu;
    insn->l        = 0u;
    insn->offset   = static_cast<int32_t>(((off11 ^ 0x400u) - 0x400u) << 1);
    insn->place_fn = &PlaceBranch;
    return true;
}

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 10 (p. A7-27). */
bool ThumbDecoder::DecodeBranchLinkPrefix(DecodedInsn* insn, uint16_t op) {
    const uint32_t off11 = op & 0x7FFu;
    insn->offset   = static_cast<int32_t>(((off11 ^ 0x400u) - 0x400u) << 12);
    insn->place_fn = &PlaceThumbBlPrefix;
    return true;
}

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 11 (p. A7-27). */
bool ThumbDecoder::DecodeBranchLinkSuffix(DecodedInsn* insn, uint16_t op) {
    insn->offset   = static_cast<int32_t>((op & 0x7FFu) << 1);
    insn->place_fn = &PlaceThumbBlSuffix;
    return true;
}
