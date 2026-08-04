#pragma once

#include <cstdint>

struct BlockContext;
struct DecodedInsn;

using ArmPlaceFn = uint8_t* (*)(uint8_t* cursor, DecodedInsn* d,
                                BlockContext* ctx);

struct DecodedInsn {
    ArmPlaceFn place_fn;
    uint32_t   guest_address;
    uint32_t   actual_guest_address;
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
    uint32_t   x1;

    uint32_t   cp_num;
    uint32_t   cp_opc;
    uint32_t   cp;
    uint32_t   crn;
    uint32_t   crm;
    uint32_t   crd;

    bool       r15_modified;
    bool       is_exception_return;
};
