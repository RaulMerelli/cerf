#include "mips_decoder.h"

#include "mips_opcode.h"

namespace {

bool RecognizedSpecial(uint32_t funct) {
    switch (funct) {
        case MipsSpecial::kSLL:  case MipsSpecial::kSRL:  case MipsSpecial::kSRA:
        case MipsSpecial::kSLLV: case MipsSpecial::kSRLV: case MipsSpecial::kSRAV:
        case MipsSpecial::kJR:   case MipsSpecial::kJALR:
        case MipsSpecial::kMOVZ: case MipsSpecial::kMOVN:
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
            return true;
        default:
            return false;
    }
}

bool RecognizedCop0(const MipsDecodedInsn* d) {
    if (d->rs == MipsCop0Rs::kMFC0 || d->rs == MipsCop0Rs::kMTC0) {
        return true;
    }
    if (d->rs >= MipsCop0Rs::kCO) {     /* CO bit set: dispatch on funct */
        switch (d->funct) {
            case MipsCop0Funct::kTLBR:  case MipsCop0Funct::kTLBWI:
            case MipsCop0Funct::kTLBWR: case MipsCop0Funct::kTLBP:
            case MipsCop0Funct::kERET:  case MipsCop0Funct::kDERET:
            case MipsCop0Funct::kWAIT:
                return true;
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
    d->entry_point        = nullptr;
    d->jmp_fixup_location = nullptr;

    switch (d->op) {
        case MipsOp::kSPECIAL:
            if (d->funct == MipsSpecial::kJR || d->funct == MipsSpecial::kJALR) {
                d->is_branch = 1;
            }
            return RecognizedSpecial(d->funct);

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
            return RecognizedCop0(d);

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

        /* I-type ALU + load/store + cache hint. */
        case MipsOp::kADDI:  case MipsOp::kADDIU:
        case MipsOp::kSLTI:  case MipsOp::kSLTIU:
        case MipsOp::kANDI:  case MipsOp::kORI:
        case MipsOp::kXORI:  case MipsOp::kLUI:
        case MipsOp::kLB:    case MipsOp::kLH:    case MipsOp::kLWL:
        case MipsOp::kLW:    case MipsOp::kLBU:   case MipsOp::kLHU:
        case MipsOp::kLWR:   case MipsOp::kSB:    case MipsOp::kSH:
        case MipsOp::kSWL:   case MipsOp::kSW:    case MipsOp::kSWR:
        case MipsOp::kLL:    case MipsOp::kSC:
        case MipsOp::kCACHE: case MipsOp::kPREF:
            return true;

        default:
            return false;   /* incl. COP1 (FPU): soft-float, CU1 never set */
    }
}
