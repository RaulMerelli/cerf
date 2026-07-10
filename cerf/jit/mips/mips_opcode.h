#pragma once

#include <cstdint>

/* MIPS IV opcode field values. Source: QEMU target/mips translate.c OPC_*
   enum, cross-checked against the MIPS IV ISA. Values are the raw field
   contents the decoder compares (major op = word>>26; SPECIAL funct =
   word&0x3f; REGIMM rt = (word>>16)&0x1f; COP0 rs = (word>>21)&0x1f). */

namespace MipsOp {            /* major opcode, bits 31..26 */
    constexpr uint32_t kSPECIAL = 0x00;
    constexpr uint32_t kREGIMM  = 0x01;
    constexpr uint32_t kJ       = 0x02;
    constexpr uint32_t kJAL     = 0x03;
    constexpr uint32_t kBEQ     = 0x04;
    constexpr uint32_t kBNE     = 0x05;
    constexpr uint32_t kBLEZ    = 0x06;
    constexpr uint32_t kBGTZ    = 0x07;
    constexpr uint32_t kADDI    = 0x08;
    constexpr uint32_t kADDIU   = 0x09;
    constexpr uint32_t kSLTI    = 0x0A;
    constexpr uint32_t kSLTIU   = 0x0B;
    constexpr uint32_t kANDI    = 0x0C;
    constexpr uint32_t kORI     = 0x0D;
    constexpr uint32_t kXORI    = 0x0E;
    constexpr uint32_t kLUI     = 0x0F;
    constexpr uint32_t kCOP0    = 0x10;
    constexpr uint32_t kCOP1    = 0x11;   /* FPU - soft-float build never enables CU1 */
    constexpr uint32_t kBEQL    = 0x14;
    constexpr uint32_t kBNEL    = 0x15;
    constexpr uint32_t kBLEZL   = 0x16;
    constexpr uint32_t kBGTZL   = 0x17;
    constexpr uint32_t kDADDI   = 0x18;   /* doubleword add immediate (64-bit, traps on overflow) */
    constexpr uint32_t kDADDIU  = 0x19;   /* doubleword add immediate unsigned (64-bit, no trap) */
    constexpr uint32_t kLDL     = 0x1A;   /* load doubleword left (unaligned, 64-bit) */
    constexpr uint32_t kLDR     = 0x1B;   /* load doubleword right (unaligned, 64-bit) */
    constexpr uint32_t kLB      = 0x20;
    constexpr uint32_t kLH      = 0x21;
    constexpr uint32_t kLWL     = 0x22;
    constexpr uint32_t kLW      = 0x23;
    constexpr uint32_t kLBU     = 0x24;
    constexpr uint32_t kLHU     = 0x25;
    constexpr uint32_t kLWR     = 0x26;
    constexpr uint32_t kLWU     = 0x27;   /* load word unsigned (MIPS III/64, zero-extend) */
    constexpr uint32_t kSB      = 0x28;
    constexpr uint32_t kSH      = 0x29;
    constexpr uint32_t kSWL     = 0x2A;
    constexpr uint32_t kSW      = 0x2B;
    constexpr uint32_t kSDL     = 0x2C;   /* store doubleword left (unaligned) */
    constexpr uint32_t kSDR     = 0x2D;   /* store doubleword right (unaligned) */
    constexpr uint32_t kSWR     = 0x2E;
    constexpr uint32_t kCACHE   = 0x2F;
    constexpr uint32_t kLD      = 0x37;   /* load doubleword (64-bit) */
    constexpr uint32_t kLL      = 0x30;
    constexpr uint32_t kLWC1    = 0x31;
    constexpr uint32_t kPREF    = 0x33;
    constexpr uint32_t kSC      = 0x38;
    constexpr uint32_t kSWC1    = 0x39;
    constexpr uint32_t kSD      = 0x3F;   /* store doubleword (64-bit) */
}

namespace MipsSpecial {       /* funct, bits 5..0 (op == SPECIAL) */
    constexpr uint32_t kSLL     = 0x00;
    constexpr uint32_t kSRL     = 0x02;
    constexpr uint32_t kSRA     = 0x03;
    constexpr uint32_t kSLLV    = 0x04;
    constexpr uint32_t kSRLV    = 0x06;
    constexpr uint32_t kSRAV    = 0x07;
    constexpr uint32_t kJR      = 0x08;
    constexpr uint32_t kJALR    = 0x09;
    constexpr uint32_t kMOVZ    = 0x0A;
    constexpr uint32_t kMOVN    = 0x0B;
    constexpr uint32_t kSYSCALL = 0x0C;
    constexpr uint32_t kBREAK   = 0x0D;
    constexpr uint32_t kSYNC    = 0x0F;
    constexpr uint32_t kMFHI    = 0x10;
    constexpr uint32_t kMTHI    = 0x11;
    constexpr uint32_t kMFLO    = 0x12;
    constexpr uint32_t kMTLO    = 0x13;
    constexpr uint32_t kMULT    = 0x18;
    constexpr uint32_t kMULTU   = 0x19;
    constexpr uint32_t kDIV     = 0x1A;
    constexpr uint32_t kDIVU    = 0x1B;
    constexpr uint32_t kADD     = 0x20;
    constexpr uint32_t kADDU    = 0x21;
    constexpr uint32_t kSUB     = 0x22;
    constexpr uint32_t kSUBU    = 0x23;
    constexpr uint32_t kDADD    = 0x2C;   /* doubleword add (64-bit, traps on overflow) */
    constexpr uint32_t kDADDU   = 0x2D;   /* doubleword add unsigned (64-bit, no trap) */
    constexpr uint32_t kDSUB    = 0x2E;   /* doubleword subtract (64-bit, traps on overflow) */
    constexpr uint32_t kAND     = 0x24;
    constexpr uint32_t kOR      = 0x25;
    constexpr uint32_t kXOR     = 0x26;
    constexpr uint32_t kNOR     = 0x27;
    constexpr uint32_t kSLT     = 0x2A;
    constexpr uint32_t kSLTU    = 0x2B;
    constexpr uint32_t kDSUBU   = 0x2F;   /* doubleword subtract unsigned (64-bit, no trap) */
    constexpr uint32_t kDMULT   = 0x1C;   /* doubleword signed multiply -> {HI,LO} (128-bit) */
    constexpr uint32_t kDMULTU  = 0x1D;   /* doubleword unsigned multiply -> {HI,LO} (128-bit) */
    constexpr uint32_t kDDIV    = 0x1E;   /* doubleword signed divide -> LO=quot HI=rem */
    constexpr uint32_t kDDIVU   = 0x1F;   /* doubleword unsigned divide -> LO=quot HI=rem */
    constexpr uint32_t kDSLLV   = 0x14;   /* doubleword variable shift left logical (count rs&63) */
    constexpr uint32_t kDSRLV   = 0x16;   /* doubleword variable shift right logical (count rs&63) */
    constexpr uint32_t kDSRAV   = 0x17;   /* doubleword variable shift right arithmetic (count rs&63) */
    constexpr uint32_t kDSLL    = 0x38;   /* doubleword shift left logical (64-bit, sa 0..31) */
    constexpr uint32_t kDSRA    = 0x3B;   /* doubleword shift right arithmetic (64-bit, sa 0..31) */
    constexpr uint32_t kDSRA32  = 0x3F;   /* doubleword shift right arithmetic +32 (64-bit) */
    constexpr uint32_t kDSRL    = 0x3A;   /* doubleword shift right logical (64-bit, sa 0..31) */
    constexpr uint32_t kDSLL32  = 0x3C;   /* doubleword shift left logical +32 (64-bit) */
    constexpr uint32_t kDSRL32  = 0x3E;   /* doubleword shift right logical +32 (R bit = DROTR32) */
}

namespace MipsRegimm {        /* rt, bits 20..16 (op == REGIMM) */
    constexpr uint32_t kBLTZ    = 0x00;
    constexpr uint32_t kBGEZ    = 0x01;
    constexpr uint32_t kBLTZL   = 0x02;
    constexpr uint32_t kBGEZL   = 0x03;
    constexpr uint32_t kBLTZAL  = 0x10;
    constexpr uint32_t kBGEZAL  = 0x11;
}

namespace MipsCop0Rs {        /* rs, bits 25..21 (op == COP0) */
    constexpr uint32_t kMFC0  = 0x00;
    constexpr uint32_t kDMFC0 = 0x01;  /* doubleword move from CP0 (64-bit GPR side) */
    constexpr uint32_t kMTC0  = 0x04;
    constexpr uint32_t kDMTC0 = 0x05;  /* doubleword move to CP0 (64-bit GPR side) */
    constexpr uint32_t kCO    = 0x10;  /* rs bit 25 set => dispatch on funct below */
}

namespace MipsCop0Funct {     /* funct, bits 5..0 (COP0 CO operations) */
    constexpr uint32_t kTLBR  = 0x01;
    constexpr uint32_t kTLBWI = 0x02;
    constexpr uint32_t kTLBWR = 0x06;
    constexpr uint32_t kTLBP  = 0x08;
    constexpr uint32_t kRFE   = 0x10;  /* qemu target/mips translate.c OPC_RFE */
    constexpr uint32_t kERET  = 0x18;
    constexpr uint32_t kDERET = 0x1F;
    constexpr uint32_t kWAIT  = 0x20;
    constexpr uint32_t kSTANDBY = 0x21;  /* VR4100 power-down ops (UM ch.27 p643) */
    constexpr uint32_t kSUSPEND = 0x22;
    constexpr uint32_t kHIBERNATE = 0x23;  /* UM ch.27 p587: COP0 CO=1, funct 100011 */
}
