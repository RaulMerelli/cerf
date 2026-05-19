#include "translate_helpers.h"

#include <cstddef>
#include <cstring>

#include "cpu_state.h"
#include "x86_emit.h"

namespace {

constexpr int32_t kCpuStateX86FlagsOfs    = static_cast<int32_t>(offsetof(ArmCpuState, x86_flags));
constexpr int32_t kCpuStateX86OverflowOfs = static_cast<int32_t>(offsetof(ArmCpuState, x86_overflow));

constexpr uint16_t kAhStoreToX86FlagsPrefix =
    static_cast<uint16_t>(0x88u) | (static_cast<uint16_t>(0xA6u) << 8);

inline bool PrevInsnStoredFlagsToX86Flags(const uint8_t* cursor) {
    uint16_t prefix;
    std::memcpy(&prefix, cursor - 6, 2);
    if (prefix != kAhStoreToX86FlagsPrefix) return false;
    int32_t disp;
    std::memcpy(&disp, cursor - 4, 4);
    return disp == kCpuStateX86FlagsOfs;
}

/* SAHF — opcode 9E, no operands. */
inline void EmitSahf(uint8_t*& p) { x86::Emit8(p, 0x9E); }

/* ROR AH, 1 — D0 /1 with modrm mod=11 reg=1 r/m=AH(4).
   modrm = (11 << 6) | (1 << 3) | 4 = 0xCC. */
inline void EmitRorAh1(uint8_t*& p) {
    x86::Emit8(p, 0xD0);
    x86::EmitModRmReg(p, 3, 4, 1);
}

/* Load packed flags into AH from ArmCpuState::x86_flags + SAHF.
   Used when the peephole optimization does NOT apply. */
inline void EmitLoadX86FlagsToAhAndSahf(uint8_t*& p) {
    x86::EmitMovByteRegBaseDisp32(p, x86::kAh, x86::kStateReg, kCpuStateX86FlagsOfs);
    EmitSahf(p);
}

/* Load V bit from ArmCpuState::x86_overflow + ROR AH, 1 — restores
   the host EFLAGS.OF for the upcoming JO/JNO. Used by VS/VC and as
   a prefix for GE/LT/GT/LE which need both NZCV and V. */
inline void EmitLoadOverflowAndRorAh(uint8_t*& p) {
    x86::EmitMovByteRegBaseDisp32(p, x86::kAh, x86::kStateReg, kCpuStateX86OverflowOfs);
    EmitRorAh1(p);
}

/* GE/LT/GT/LE form: load saved V into AL (not AH) + ROR AL, then
   load flags into AH + SAHF. Two-byte cluster: AL holds OF source
   for the signed-compare encodings. */
inline void EmitLoadOverflowToAlAndRorAl(uint8_t*& p) {
    x86::EmitMovByteRegBaseDisp32(p, x86::kAl, x86::kStateReg, kCpuStateX86OverflowOfs);
    /* ROR AL, 1 — D0 /1 modrm mod=11 reg=1 r/m=AL(0) = 0xC8. */
    x86::Emit8(p, 0xD0);
    x86::EmitModRmReg(p, 3, 0, 1);
}

}  // namespace

uint8_t* TranslateNop(uint8_t* code_cursor, DecodedInsn* /*d*/, BlockContext* /*ctx*/) {
    return code_cursor;
}

uint8_t* PlaceConditionCheck(uint8_t* cursor, const DecodedInsn* d, BlockContext* ctx) {
    /* Peephole: AH still holds packed flags if the previous insn in
       the same entrypoint was unconditional and ended with a store
       of AH to ArmCpuState::x86_flags. */
    bool flags_loaded     = false;
    bool all_flags_set    = false;

    if (d->cond < 14u &&
        d != &ctx->insns[0] &&
        d->entry_point == (d - 1)->entry_point &&
        (d - 1)->cond == 14u &&
        PrevInsnStoredFlagsToX86Flags(cursor)) {
        flags_loaded  = true;
        if ((d - 1)->flags_set == kFlagsAll) {
            all_flags_set = true;
        }
    }

    /* Each branch below appends one (or two) fixup labels to
       ctx->big_skips* and increments big_skip_count. label points
       past the end of the rel32 displacement; FixupLabel32 patches
       (label - 4..label - 1) when the run ends. */
    uint8_t* skip1 = nullptr;
    uint8_t* skip2 = nullptr;

    switch (d->cond) {
    case 0:  /* EQ — Z set ⇒ skip if Z clear */
        if (!flags_loaded) EmitLoadX86FlagsToAhAndSahf(cursor);
        else if (!all_flags_set) EmitSahf(cursor);
        skip1 = x86::EmitJnzLabel32(cursor);
        break;

    case 1:  /* NE — Z clear ⇒ skip if Z set */
        if (!flags_loaded) EmitLoadX86FlagsToAhAndSahf(cursor);
        else if (!all_flags_set) EmitSahf(cursor);
        skip1 = x86::EmitJzLabel32(cursor);
        break;

    case 2:  /* CS / HS — C set ⇒ skip if C clear */
        if (!flags_loaded) {
            x86::EmitBtBaseDisp32Imm(cursor, x86::kStateReg, kCpuStateX86FlagsOfs, 0);
        } else if (!all_flags_set) {
            EmitSahf(cursor);
        }
        skip1 = x86::EmitJncLabel32(cursor);
        break;

    case 3:  /* CC / LO — C clear ⇒ skip if C set */
        if (!flags_loaded) {
            x86::EmitBtBaseDisp32Imm(cursor, x86::kStateReg, kCpuStateX86FlagsOfs, 0);
        } else if (!all_flags_set) {
            EmitSahf(cursor);
        }
        skip1 = x86::EmitJcLabel32(cursor);
        break;

    case 4:  /* MI — N set ⇒ skip if N clear */
        if (!flags_loaded) EmitLoadX86FlagsToAhAndSahf(cursor);
        else if (!all_flags_set) EmitSahf(cursor);
        skip1 = x86::EmitJnsLabel32(cursor);
        break;

    case 5:  /* PL — N clear ⇒ skip if N set */
        if (!flags_loaded) EmitLoadX86FlagsToAhAndSahf(cursor);
        else if (!all_flags_set) EmitSahf(cursor);
        skip1 = x86::EmitJsLabel32(cursor);
        break;

    case 6:  /* VS — V set ⇒ skip if V clear */
        EmitLoadOverflowAndRorAh(cursor);
        skip1 = x86::EmitJnoLabel32(cursor);
        break;

    case 7:  /* VC — V clear ⇒ skip if V set */
        EmitLoadOverflowAndRorAh(cursor);
        skip1 = x86::EmitJoLabel32(cursor);
        break;

    case 8:  /* HI — C set AND Z clear. Skip if NOT(HI) = C clear OR Z set. */
        if (!flags_loaded) EmitLoadX86FlagsToAhAndSahf(cursor);
        else if (!all_flags_set) EmitSahf(cursor);
        /* Flip CF, then JBE (CF set OR ZF set). */
        x86::EmitCmc(cursor);
        skip1 = x86::EmitJbeLabel32(cursor);
        break;

    case 9:  /* LS — C clear OR Z set. Skip if NOT(LS) = C set AND Z clear. */
        if (!flags_loaded) EmitLoadX86FlagsToAhAndSahf(cursor);
        else if (!all_flags_set) EmitSahf(cursor);
        x86::EmitCmc(cursor);
        skip1 = x86::EmitJaLabel32(cursor);
        break;

    case 10: /* GE — N == V. Skip if N != V. */
        if (!flags_loaded) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitLoadX86FlagsToAhAndSahf(cursor);
        } else if (!all_flags_set) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitSahf(cursor);
        }
        skip1 = x86::EmitJlLabel32(cursor);
        break;

    case 11: /* LT — N != V. Skip if N == V. */
        if (!flags_loaded) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitLoadX86FlagsToAhAndSahf(cursor);
        } else if (!all_flags_set) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitSahf(cursor);
        }
        skip1 = x86::EmitJgeLabel32(cursor);
        break;

    case 12: /* GT — Z clear AND N == V. Skip if Z set OR N != V. */
        if (!flags_loaded) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitLoadX86FlagsToAhAndSahf(cursor);
        } else if (!all_flags_set) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitSahf(cursor);
        }
        skip1 = x86::EmitJleLabel32(cursor);
        break;

    case 13: /* LE — Z set OR N != V. Skip if Z clear AND N == V. */
        if (!flags_loaded) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitLoadX86FlagsToAhAndSahf(cursor);
        } else if (!all_flags_set) {
            EmitLoadOverflowToAlAndRorAl(cursor);
            EmitSahf(cursor);
        }
        skip1 = x86::EmitJgLabel32(cursor);
        break;
    }

    /* Write the fixup labels into the caller-reserved slot at
       big_skip_count. The caller increments big_skip_count after
       this returns (reference contract). */
    ctx->big_skips1[ctx->big_skip_count] = skip1;
    ctx->big_skips2[ctx->big_skip_count] = skip2;
    return cursor;
}

void PlaceEndConditionCheck(uint8_t* cursor, BlockContext* ctx) {
    while (ctx->big_skip_count > 0) {
        --ctx->big_skip_count;
        uint8_t* s1 = ctx->big_skips1[ctx->big_skip_count];
        uint8_t* s2 = ctx->big_skips2[ctx->big_skip_count];
        if (s1) x86::FixupLabel32(s1, cursor);
        if (s2) x86::FixupLabel32(s2, cursor);
    }
}

