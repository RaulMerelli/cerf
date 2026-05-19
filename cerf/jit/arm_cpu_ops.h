#pragma once

#include <cstdint>

#include "cpu_state.h"

class ArmJit;

/* Call before updating CPSR.M — banks R13/R14 based on the OLD
   mode in state->cpsr; calling AFTER the mode update parks the
   register file into the wrong slot and the old mode's banked
   R13/R14 are lost. */
void ArmCpuBankSwitch(ArmCpuState* state);

uint32_t ArmCpuGetCpsrWithFlags(const ArmCpuState* state);

void ArmCpuUpdateFlags(ArmCpuState* state, uint32_t new_flag_value);

/* Skips CPSR.SaturateFlag (FPSCR.QC at bit 27 is not forwarded to
   APSR.Q by VMRS R15, FPSCR — only NZCV is — per QEMU
   translate-vfp.c:1459-1461 quoted in
   references/omap3530/armv7_arch_excerpts.txt § VFP system regs). */
void ArmCpuUpdateNzcvOnly(ArmCpuState* state, uint32_t new_flag_value);

/* Two bank swaps on mode change: pre-swap parks OLD-mode R13/R14,
   post-swap loads NEW-mode R13/R14 — inverting the order loses
   the OLD bank and reads NEW from uninitialised storage. */
void ArmCpuUpdateCpsrWithFlags(ArmJit* jit, ArmCpuState* state, ArmPsrFull new_psr);

void ArmCpuUpdateCpsr(ArmJit* jit, ArmCpuState* state, ArmPsr new_psr);
