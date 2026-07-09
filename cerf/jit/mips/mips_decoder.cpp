#include "mips_decoder.h"

#include "mips_opcode.h"

namespace {

bool RecognizedSpecial(uint32_t funct, bool has_mips4) {
    switch (funct) {
        case MipsSpecial::kSLL:  case MipsSpecial::kSRL:  case MipsSpecial::kSRA:
        case MipsSpecial::kSLLV: case MipsSpecial::kSRLV: case MipsSpecial::kSRAV:
        case MipsSpecial::kJR:   case MipsSpecial::kJALR:
        case MipsSpecial::kSYSCALL: case MipsSpecial::kBREAK: case MipsSpecial::kSYNC:
        case MipsSpecial::kMFHI: case MipsSpecial::kMTHI:
        case MipsSpecial::kMFLO: case MipsSpecial::kMTLO:
        case MipsSpecial::kMULT: case MipsSpecial::kMULTU:
        case MipsSpecial::kDIV:  case MipsSpecial::kDIVU:
        case MipsSpecial::kADD:  case MipsSpecial::kADDU:
        case MipsSpecial::kSUB:  case MipsSpecial::kSUBU:
        case MipsSpecial::kAND:  case MipsSpecial::kOR:
        case MipsSpecial::kXOR:  case MipsSpecial::kNOR:
        case MipsSpecial::kSLT:  case MipsSpecial::kSLTU:
        case MipsSpecial::kDADD:
        case MipsSpecial::kDADDU:
        case MipsSpecial::kDSUB:
        case MipsSpecial::kDSUBU:
        case MipsSpecial::kDMULT:
        case MipsSpecial::kDMULTU:
        case MipsSpecial::kDDIV:
        case MipsSpecial::kDDIVU:
        case MipsSpecial::kDSLLV:
        case MipsSpecial::kDSRLV:
        case MipsSpecial::kDSRAV:
        case MipsSpecial::kDSLL:
        case MipsSpecial::kDSRL:
        case MipsSpecial::kDSRA:
        case MipsSpecial::kDSLL32:
        case MipsSpecial::kDSRL32:
        case MipsSpecial::kDSRA32:
            return true;
        case MipsSpecial::kMOVZ:
        case MipsSpecial::kMOVN:
            return has_mips4;   /* MIPS IV conditional moves; absent on VR4102 (MIPS III) */
        default:
            return false;
    }
}

bool RecognizedCop0(const MipsDecodedInsn* d, bool has_vr41xx_power_modes) {
    if (d->rs == MipsCop0Rs::kMFC0  || d->rs == MipsCop0Rs::kMTC0 ||
        d->rs == MipsCop0Rs::kDMFC0 || d->rs == MipsCop0Rs::kDMTC0) {
        return true;
    }
    if (d->rs >= MipsCop0Rs::kCO) {     /* CO bit set: dispatch on funct */
        switch (d->funct) {
            case MipsCop0Funct::kTLBR:  case MipsCop0Funct::kTLBWI:
            case MipsCop0Funct::kTLBWR: case MipsCop0Funct::kTLBP:
            case MipsCop0Funct::kERET:  case MipsCop0Funct::kDERET:
            case MipsCop0Funct::kWAIT:
                return true;
            case MipsCop0Funct::kSTANDBY: case MipsCop0Funct::kSUSPEND:
            case MipsCop0Funct::kHIBERNATE:
                return has_vr41xx_power_modes;
            default:
                return false;
        }
    }
    return false;
}

}  // namespace

bool MipsDecoder::Decode(uint32_t word, uint32_t pc, MipsDecodedInsn* d) {
    d->place_fn           = nullptr;
    d->guest_address      = pc;
    d->raw                = word;
    d->op                 = word >> 26;
    d->rs                 = (word >> 21) & 0x1f;
    d->rt                 = (word >> 16) & 0x1f;
    d->rd                 = (word >> 11) & 0x1f;
    d->sa                 = (word >> 6) & 0x1f;
    d->funct              = word & 0x3f;
    d->imm                = word & 0xffff;
    d->target             = word & 0x03ffffff;
    d->is_branch          = 0;
    d->is_likely          = 0;
    d->ends_block         = 0;
    d->entry_point        = nullptr;
    d->jmp_fixup_location = nullptr;

    switch (d->op) {
        case MipsOp::kSPECIAL:
            if (d->funct == MipsSpecial::kJR || d->funct == MipsSpecial::kJALR) {
                d->is_branch = 1;
            }
            return RecognizedSpecial(d->funct, has_mips4_);

        case MipsOp::kREGIMM:
            switch (d->rt) {
                case MipsRegimm::kBLTZ:  case MipsRegimm::kBGEZ:
                case MipsRegimm::kBLTZAL: case MipsRegimm::kBGEZAL:
                    d->is_branch = 1;
                    return true;
                case MipsRegimm::kBLTZL: case MipsRegimm::kBGEZL:
                    d->is_branch = 1;
                    d->is_likely = 1;
                    return true;
                default:
                    return false;
            }

        case MipsOp::kCOP0:
            if (d->rs >= MipsCop0Rs::kCO &&
                (d->funct == MipsCop0Funct::kERET ||
                 (d->funct == MipsCop0Funct::kHIBERNATE && has_vr41xx_power_modes_))) {
                d->ends_block = 1;   /* neither has a delay slot */
            }
            return RecognizedCop0(d, has_vr41xx_power_modes_);

        /* Branches with a delay slot. */
        case MipsOp::kBEQ:  case MipsOp::kBNE:
        case MipsOp::kBLEZ: case MipsOp::kBGTZ:
        case MipsOp::kJ:    case MipsOp::kJAL:
            d->is_branch = 1;
            return true;
        case MipsOp::kBEQL: case MipsOp::kBNEL:
        case MipsOp::kBLEZL: case MipsOp::kBGTZL:
            d->is_branch = 1;
            d->is_likely = 1;
            return true;

        /* CP1 (FPU) coprocessor: present iff the SoC has an FPU. On a no-FPU /
           soft-float build COP1 is a Reserved/Coprocessor-Unusable encoding. */
        case MipsOp::kCOP1:
            return has_fpu_;

        /* LL / SC atomics: present iff the SoC implements them. */
        case MipsOp::kLL:    case MipsOp::kSC:
            return has_llsc_;

        /* I-type ALU + load/store + cache hint. */
        case MipsOp::kADDI:  case MipsOp::kADDIU:
        case MipsOp::kDADDI: case MipsOp::kDADDIU:
        case MipsOp::kSLTI:  case MipsOp::kSLTIU:
        case MipsOp::kANDI:  case MipsOp::kORI:
        case MipsOp::kXORI:  case MipsOp::kLUI:
        case MipsOp::kLB:    case MipsOp::kLH:    case MipsOp::kLWL:
        case MipsOp::kLW:    case MipsOp::kLBU:   case MipsOp::kLHU:
        case MipsOp::kLWR:   case MipsOp::kLWU:   case MipsOp::kSB:    case MipsOp::kSH:
        case MipsOp::kSWL:   case MipsOp::kSW:    case MipsOp::kSWR:
        case MipsOp::kSDL:   case MipsOp::kSDR:   case MipsOp::kLDL:   case MipsOp::kLDR:
        case MipsOp::kLD:    case MipsOp::kSD:
        case MipsOp::kCACHE:
            return true;
        case MipsOp::kPREF:
            return has_mips4_;   /* PREF is MIPS IV; CACHE is MIPS III */

        default:
            return false;   /* reserved encoding for this CPU */
    }
}
