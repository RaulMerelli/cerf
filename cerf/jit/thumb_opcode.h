#pragma once

#include <cstdint>

union ThumbOpcode {
    uint16_t half_word;

    struct {
        uint16_t reserved : 13;
        uint16_t opcode   : 3;
    } generic;

    struct {  /* LSL / LSR / ASR with 5-bit immediate. */
        uint16_t rd       : 3;
        uint16_t rs       : 3;
        uint16_t offset5  : 5;
        uint16_t op2      : 2;
        uint16_t opcode   : 3;
    } move_shifted_register;

    struct {  /* ADD / SUB with 3-bit immediate or register. */
        uint16_t rd        : 3;
        uint16_t rs        : 3;
        uint16_t rn_offset : 3;
        uint16_t op        : 1;
        uint16_t i         : 1;
        uint16_t reserved  : 2;
        uint16_t opcode    : 3;
    } add_subtract;

    struct {  /* MOV / CMP / ADD / SUB with 8-bit immediate. */
        uint16_t offset8 : 8;
        uint16_t rd      : 3;
        uint16_t op      : 2;
        uint16_t opcode  : 3;
    } math_immediate;

    struct {  /* AND / EOR / LSL / LSR / ASR / ADC / SBC / ROR / TST /
                 NEG / CMP / CMN / ORR / MUL / BIC / MVN. */
        uint16_t rd       : 3;
        uint16_t rs       : 3;
        uint16_t op       : 4;
        uint16_t reserved : 3;
        uint16_t opcode   : 3;
    } alu_operation;

    struct {  /* Hi-register ADD / CMP / MOV / BX. */
        uint16_t rd_hd    : 3;
        uint16_t rs_hs    : 3;
        uint16_t h2       : 1;
        uint16_t h1       : 1;
        uint16_t op       : 2;
        uint16_t reserved : 3;
        uint16_t opcode   : 3;
    } hi_ops;

    struct {  /* LDR Rd, [PC + #imm]. */
        uint16_t word8    : 8;
        uint16_t rd       : 3;
        uint16_t reserved : 2;
        uint16_t opcode   : 3;
    } pc_relative_load;

    struct {  /* LDR/STR Rd, [Rb, Ro]; LDRB/STRB. */
        uint16_t rd        : 3;
        uint16_t rb        : 3;
        uint16_t ro        : 3;
        uint16_t reserved1 : 1;
        uint16_t b         : 1;
        uint16_t l         : 1;
        uint16_t reserved2 : 1;
        uint16_t opcode    : 3;
    } load_store_register_offset;

    struct {  /* LDRH/STRH/LDRSB/LDRSH with register offset. */
        uint16_t rd        : 3;
        uint16_t rb        : 3;
        uint16_t ro        : 3;
        uint16_t reserved1 : 1;
        uint16_t s         : 1;
        uint16_t h         : 1;
        uint16_t reserved2 : 1;
        uint16_t opcode    : 3;
    } load_store_byte_half_word;

    struct {  /* LDR/STR Rd, [Rb, #imm]; LDRB/STRB. */
        uint16_t rd      : 3;
        uint16_t rb      : 3;
        uint16_t offset5 : 5;
        uint16_t l       : 1;
        uint16_t b       : 1;
        uint16_t opcode  : 3;
    } load_store_immediate_offset;

    struct {  /* LDRH/STRH Rd, [Rb, #imm]. */
        uint16_t rd        : 3;
        uint16_t rb        : 3;
        uint16_t offset5   : 5;
        uint16_t l         : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } load_store_half_word;

    struct {  /* LDR/STR Rd, [SP + #imm]. */
        uint16_t word8     : 8;
        uint16_t rd        : 3;
        uint16_t l         : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } load_store_sp_relative;

    struct {  /* ADD Rd, PC, #imm OR ADD Rd, SP, #imm. */
        uint16_t word8     : 8;
        uint16_t rd        : 3;
        uint16_t sp        : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } load_address;

    struct {  /* ADD/SUB SP, #imm. */
        uint16_t s_word7   : 7;
        uint16_t s         : 1;
        uint16_t reserved1 : 5;
        uint16_t opcode    : 3;
    } add_to_stack_pointer;

    struct {  /* PUSH / POP {Rlist, R} — R selects LR (push) or PC (pop). */
        uint16_t r_list    : 8;
        uint16_t r         : 1;
        uint16_t reserved1 : 2;
        uint16_t l         : 1;
        uint16_t reserved2 : 1;
        uint16_t opcode    : 3;
    } push_pop_registers;

    struct {  /* LDMIA / STMIA Rb!, {Rlist}. */
        uint16_t r_list    : 8;
        uint16_t rb        : 3;
        uint16_t l         : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } multiple_load_store;

    struct {  /* Conditional branch (Bxx) — encoded with 8-bit signed
                 offset; cond == 0xF selects SWI. */
        int16_t  s_offset8 : 8;
        uint16_t cond      : 4;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } conditional_branch;

    struct {  /* SWI / SVC. */
        uint16_t value8    : 8;
        uint16_t reserved1 : 5;
        uint16_t opcode    : 3;
    } software_interrupt;

    struct {  /* Unconditional branch — 11-bit signed offset. */
        int16_t  offset11  : 11;
        uint16_t reserved1 : 2;
        uint16_t opcode    : 3;
    } unconditional_branch;

    struct {  /* BL — two-instruction long branch; H selects which half. */
        uint16_t offset : 11;
        uint16_t h      : 2;
        uint16_t opcode : 3;
    } long_branch;
};
static_assert(sizeof(ThumbOpcode) == 2, "ThumbOpcode must be 16 bits");
