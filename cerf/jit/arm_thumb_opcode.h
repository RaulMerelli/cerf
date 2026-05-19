#pragma once

#include <cstdint>

union ArmThumbOpcode {
    uint16_t half_word;

    struct {
        uint16_t reserved : 13;
        uint16_t opcode   : 3;
    } generic;

    /* LSL / LSR / ASR (immediate). */
    struct {
        uint16_t rd       : 3;
        uint16_t rs       : 3;
        uint16_t offset5  : 5;
        uint16_t op2      : 2;
        uint16_t opcode   : 3;
    } move_shifted_register;

    /* ADD / SUB (register or 3-bit immediate). */
    struct {
        uint16_t rd        : 3;
        uint16_t rs        : 3;
        uint16_t rn_offset : 3;
        uint16_t op        : 1;
        uint16_t i         : 1;
        uint16_t reserved  : 2;
        uint16_t opcode    : 3;
    } add_subtract;

    /* MOV / CMP / ADD / SUB (8-bit immediate). */
    struct {
        uint16_t offset8 : 8;
        uint16_t rd     : 3;
        uint16_t op     : 2;
        uint16_t opcode : 3;
    } math_immediate;

    /* Thumb ALU register-register form (AND/EOR/LSL/LSR/...). */
    struct {
        uint16_t rd       : 3;
        uint16_t rs       : 3;
        uint16_t op       : 4;
        uint16_t reserved : 3;
        uint16_t opcode   : 3;
    } alu_operation;

    /* Hi-register operations (ADD/CMP/MOV/BX with high regs). */
    struct {
        uint16_t rd_hd   : 3;
        uint16_t rs_hs   : 3;
        uint16_t h2      : 1;
        uint16_t h1      : 1;
        uint16_t op      : 2;
        uint16_t reserved: 3;
        uint16_t opcode  : 3;
    } hi_ops;

    /* LDR Rd, [PC, #imm]. */
    struct {
        uint16_t word8    : 8;
        uint16_t rd       : 3;
        uint16_t reserved : 2;
        uint16_t opcode   : 3;
    } pc_relative_load;

    /* LDR/STR Rd, [Rb, Ro] (word and byte). */
    struct {
        uint16_t rd        : 3;
        uint16_t rb        : 3;
        uint16_t ro        : 3;
        uint16_t reserved1 : 1;
        uint16_t b         : 1;
        uint16_t l         : 1;
        uint16_t reserved2 : 1;
        uint16_t opcode    : 3;
    } load_store_register_offset;

    /* LDRH/STRH/LDSB/LDSH Rd, [Rb, Ro] — signed/unsigned half/byte. */
    struct {
        uint16_t rd        : 3;
        uint16_t rb        : 3;
        uint16_t ro        : 3;
        uint16_t reserved1 : 1;
        uint16_t s         : 1;
        uint16_t h         : 1;
        uint16_t reserved2 : 1;
        uint16_t opcode    : 3;
    } load_store_byte_half_word;

    /* LDR/STR Rd, [Rb, #imm5*4 or #imm5]. */
    struct {
        uint16_t rd      : 3;
        uint16_t rb      : 3;
        uint16_t offset5 : 5;
        uint16_t l       : 1;
        uint16_t b       : 1;
        uint16_t opcode  : 3;
    } load_store_immediate_offset;

    /* LDRH/STRH Rd, [Rb, #imm5*2]. */
    struct {
        uint16_t rd        : 3;
        uint16_t rb        : 3;
        uint16_t offset5   : 5;
        uint16_t l         : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } load_store_half_word;

    /* LDR/STR Rd, [SP, #imm8*4]. */
    struct {
        uint16_t word8     : 8;
        uint16_t rd        : 3;
        uint16_t l         : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } load_store_sp_relative;

    /* ADD Rd, PC, #imm8*4  or  ADD Rd, SP, #imm8*4. */
    struct {
        uint16_t word8     : 8;
        uint16_t rd        : 3;
        uint16_t sp        : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } load_address;

    /* ADD/SUB SP, #imm7*4. */
    struct {
        uint16_t s_word7   : 7;
        uint16_t s         : 1;
        uint16_t reserved1 : 5;
        uint16_t opcode    : 3;
    } add_to_stack_pointer;

    /* PUSH / POP {register list} [+ R]. */
    struct {
        uint16_t rlist     : 8;
        uint16_t r         : 1;
        uint16_t reserved1 : 2;
        uint16_t l         : 1;
        uint16_t reserved2 : 1;
        uint16_t opcode    : 3;
    } push_pop_registers;

    /* LDMIA / STMIA Rb!, {register list}. */
    struct {
        uint16_t rlist     : 8;
        uint16_t rb        : 3;
        uint16_t l         : 1;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } multiple_load_store;

    /* Conditional branch Bcc label (sign-extended 8-bit offset). */
    struct {
        int16_t  s_offset8 : 8;
        uint16_t cond      : 4;
        uint16_t reserved1 : 1;
        uint16_t opcode    : 3;
    } conditional_branch;

    /* SWI imm8. */
    struct {
        uint16_t value8    : 8;
        uint16_t reserved1 : 5;
        uint16_t opcode    : 3;
    } software_interrupt;

    /* Unconditional B label (sign-extended 11-bit offset). */
    struct {
        int16_t  offset11  : 11;
        uint16_t reserved1 : 2;
        uint16_t opcode    : 3;
    } unconditional_branch;

    /* BL prefix/suffix pair — H==10 first half, H==11 second half. */
    struct {
        uint16_t offset    : 11;
        uint16_t h         : 2;
        uint16_t opcode    : 3;
    } long_branch;
};
static_assert(sizeof(ArmThumbOpcode) == 2, "ArmThumbOpcode must be 16 bits");

/* In-guest-RAM interrupt-vector table layout at the vector base
   (either 0x00000000 or 0xFFFF0000 depending on SCTLR.V). Each
   entry is one ARM instruction (LDR PC, [PC, #disp]) that
   redirects to the actual handler. */
struct ArmInterruptVectors {
    uint32_t reset_exception;                  /* SVC mode on entry      */
    uint32_t undefined_instruction_exception;  /* UND mode               */
    uint32_t software_interrupt_exception;     /* SVC mode               */
    uint32_t abort_prefetch_exception;         /* ABT mode               */
    uint32_t abort_data_exception;             /* ABT mode               */
    uint32_t reserved;
    uint32_t irq_exception;                    /* IRQ mode               */
};
