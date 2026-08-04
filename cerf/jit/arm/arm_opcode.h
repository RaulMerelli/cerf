#pragma once

#include <cstdint>

union ArmOpcode {
    uint32_t word;

    /* Advanced SIMD data-processing, ARM encoding (DDI 0406C.c A7.4
       p. A7-261): 1111 [31:28], 001 [27:25], U [24]. Field columns per
       Table A7-9 "Three registers of the same length" (A7.4.1 p. A7-262):
       A = [11:8], B = [4], C = [21:20]. */
    struct {
        uint32_t        : 4;   /* [3:0] */
        uint32_t c      : 1;   /* [4] */
        uint32_t        : 3;   /* [7:5] */
        uint32_t opc    : 4;   /* [11:8] */
        uint32_t        : 8;   /* [19:12] */
        uint32_t size   : 2;   /* [21:20] */
        uint32_t        : 1;   /* [22] */
        uint32_t bit23  : 1;   /* [23] */
        uint32_t u      : 1;   /* [24] */
        uint32_t marker : 3;   /* [27:25], 001 */
        uint32_t        : 4;   /* [31:28] */
    } neon_data_3reg;

    /* Advanced SIMD element/structure load/store, ARM encoding
       (DDI 0406C.c A7.7 p. A7-275): 1111 [31:28], 0100 [27:24], A [23],
       L [21]. Operand fields per VLD1 (multiple single elements) A1
       (A8.8.320 p. A8-898). */
    struct {
        uint32_t rm     : 4;   /* [3:0] */
        uint32_t align  : 2;   /* [5:4] */
        uint32_t size   : 2;   /* [7:6] */
        uint32_t type   : 4;   /* [11:8] */
        uint32_t vd     : 4;   /* [15:12] */
        uint32_t rn     : 4;   /* [19:16] */
        uint32_t        : 1;   /* [20] */
        uint32_t l      : 1;   /* [21] */
        uint32_t d      : 1;   /* [22] */
        uint32_t a      : 1;   /* [23] */
        uint32_t marker : 4;   /* [27:24], 0100 */
        uint32_t        : 4;   /* [31:28] */
    } neon_load_store;

    /* Single element to one lane, ARM encoding (DDI 0406C.c A8.8.321 A1
       p. A8-900). [9:8] holds Table A7-20/A7-21's B<1:0>. */
    struct {
        uint32_t rm          : 4;   /* [3:0] */
        uint32_t index_align : 4;   /* [7:4] */
        uint32_t n_minus1    : 2;   /* [9:8] */
        uint32_t size        : 2;   /* [11:10] */
        uint32_t vd          : 4;   /* [15:12] */
        uint32_t rn          : 4;   /* [19:16] */
        uint32_t             : 1;   /* [20] */
        uint32_t l           : 1;   /* [21] */
        uint32_t d           : 1;   /* [22] */
        uint32_t             : 9;   /* [31:23] */
    } neon_load_store_single;
};

static_assert(sizeof(ArmOpcode) == 4);
