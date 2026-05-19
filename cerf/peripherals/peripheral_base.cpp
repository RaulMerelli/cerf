#include "peripheral_base.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../jit/arm_cpu_ops.h"
#include "../jit/arm_jit.h"
#include "../jit/cpu_state.h"

#include <typeinfo>

uint8_t Peripheral::ReadByte(uint32_t addr) {
    LOG(Caution, "Peripheral '%s' does not support ReadByte at 0x%08X\n",
            typeid(*this).name(), addr);
    CerfFatalExit(1);
}

uint16_t Peripheral::ReadHalf(uint32_t addr) {
    LOG(Caution, "Peripheral '%s' does not support ReadHalf at 0x%08X\n",
            typeid(*this).name(), addr);
    CerfFatalExit(1);
}

uint32_t Peripheral::ReadWord(uint32_t addr) {
    LOG(Caution, "Peripheral '%s' does not support ReadWord at 0x%08X\n",
            typeid(*this).name(), addr);
    CerfFatalExit(1);
}

uint64_t Peripheral::ReadDword(uint32_t addr) {
    LOG(Caution, "Peripheral '%s' does not support ReadDword at 0x%08X\n",
            typeid(*this).name(), addr);
    CerfFatalExit(1);
}

void Peripheral::WriteByte(uint32_t addr, uint8_t value) {
    LOG(Caution, "Peripheral '%s' does not support WriteByte 0x%08X = 0x%02X\n",
            typeid(*this).name(), addr, value);
    CerfFatalExit(1);
}

void Peripheral::WriteHalf(uint32_t addr, uint16_t value) {
    LOG(Caution, "Peripheral '%s' does not support WriteHalf 0x%08X = 0x%04X\n",
            typeid(*this).name(), addr, value);
    CerfFatalExit(1);
}

void Peripheral::WriteWord(uint32_t addr, uint32_t value) {
    LOG(Caution, "Peripheral '%s' does not support WriteWord 0x%08X = 0x%08X\n",
            typeid(*this).name(), addr, value);
    CerfFatalExit(1);
}

void Peripheral::WriteDword(uint32_t addr, uint64_t value) {
    LOG(Caution, "Peripheral '%s' does not support WriteDword 0x%08X = 0x%016llX\n",
            typeid(*this).name(), addr,
            static_cast<unsigned long long>(value));
    CerfFatalExit(1);
}

void Peripheral::HaltUnsupportedAccess(const char* op,
                                       uint32_t addr,
                                       uint64_t value) const {
    LOG(Caution, "Peripheral '%s' rejected %s at 0x%08X (value 0x%016llX)\n",
            typeid(*this).name(), op, addr,
            static_cast<unsigned long long>(value));
    /* Dump JIT register state at fault time so the rejecting call site
       is visible in the log. Mirrors Mmu::RaiseAbort's diagnostic dump.
       Same pattern: every peripheral halt is a FATAL, register state at
       the halt is always useful to the next investigator. */
    auto* state      = emu_.Get<ArmJit>().CpuState();
    const auto& r    = state->gprs;
    const uint32_t c = ArmCpuGetCpsrWithFlags(state);
    LOG(Caution, "      guest PC=0x%08X CPSR=0x%08X\n", r[15], c);
    LOG(Caution, "      R0=0x%08X  R1=0x%08X  R2=0x%08X  R3=0x%08X "
                 "R4=0x%08X  R5=0x%08X  R6=0x%08X  R7=0x%08X\n",
        r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    LOG(Caution, "      R8=0x%08X  R9=0x%08X  R10=0x%08X R11=0x%08X "
                 "R12=0x%08X SP=0x%08X  LR=0x%08X\n",
        r[8], r[9], r[10], r[11], r[12], r[13], r[14]);
    CerfFatalExit(1);
}
