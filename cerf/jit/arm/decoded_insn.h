#pragma once

#include <cstdint>

struct BlockContext;
struct DecodedInsn;

using ArmPlaceFn = uint8_t* (*)(uint8_t* cursor, DecodedInsn* d,
                                BlockContext* ctx);

/* DDI 0406C.c A8.4.3 (pp. A8-292/293): SRType. LSL/LSR/ASR/ROR carry the
   instruction's own type encoding ("encoded as type = 0b00" .. 0b11);
   RRX is DecodeImmShift's imm5 == 0 rewrite of type 0b11. */
enum ArmSrType : uint32_t {
    kSrLsl = 0u,
    kSrLsr = 1u,
    kSrAsr = 2u,
    kSrRor = 3u,
    kSrRrx = 4u,
};

/* DDI 0406C.c A8.4.3 (p. A8-292): "(SRType, integer) DecodeImmShift(bits(2)
   type, bits(5) imm5)" - type 00 takes UInt(imm5); type 01 and 10 take 32 when
   imm5 == 00000; type 11 with imm5 == 00000 is SRType_RRX with shift_n 1. */
inline void DecodeImmShift(uint32_t type, uint32_t imm5, uint32_t* shift_t,
                           uint32_t* shift_n) {
    *shift_t = type;
    *shift_n = imm5;
    if ((type == kSrLsr || type == kSrAsr) && imm5 == 0u) {
        *shift_n = 32u;
    } else if (type == kSrRor && imm5 == 0u) {
        *shift_t = kSrRrx;
        *shift_n = 1u;
    }
}

/* DDI 0406C.c Table A5-5 (p. A5-199) allocates the ARM data-processing opcodes
   0..15. DDI 0406C.c Table A6-10 (p. A6-231) row 0011 with Rn != 1111 is
   Bitwise OR NOT, A8.8.120 ORN (immediate) (p. A8-512). */
enum ArmDpOpcode : uint32_t {
    kDpOrn = 16u,
};

struct DecodedInsn {
    ArmPlaceFn place_fn;
    uint32_t   guest_address;
    uint32_t   actual_guest_address;
    uint32_t   length;
    uint32_t   immediate;
    uint32_t   cond;
    uint32_t   op1;

    uint32_t   rd;
    uint32_t   rn;
    uint32_t   rm;
    uint32_t   rs;
    uint16_t   register_list;
    int32_t    offset;

    uint32_t   l;
    uint32_t   s;
    uint32_t   p;
    uint32_t   u;
    uint32_t   w;
    uint32_t   n;
    /* DDI 0406C.c A8.8.92 LDRT (p. A8-466), Table A5-15 (p. A5-208). */
    uint32_t   unpriv;

    uint32_t   cp_num;
    uint32_t   cp_opc;
    uint32_t   cp;
    uint32_t   crn;
    uint32_t   crm;
    uint32_t   crd;

    bool       r15_modified;
    bool       is_exception_return;
    bool       context_sync;
};
