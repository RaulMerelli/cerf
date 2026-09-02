#pragma once

#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

/* Intel MCS-51 User's Manual, February 1994, Chapter 2: direct addresses
   0x00-0x7F reach internal RAM and 0x80-0xFF the special function registers;
   the bit space aliases the bit-addressable subset of both. */
struct Dw8051State {
    uint16_t pc = 0;
    uint8_t sp = 0x07;
    uint8_t acc = 0;
    uint8_t b = 0;
    uint8_t psw = 0;
    uint8_t dpl = 0;
    uint8_t dph = 0;
    uint8_t p0 = 0xFF;
    uint8_t p1 = 0xFF;
    uint8_t p2 = 0xFF;
    uint8_t p3 = 0xFF;
    uint8_t ie = 0;
    uint8_t eie = 0xE0u;
    uint8_t eip = 0xE0u;
    uint8_t ip = 0;
    uint8_t tcon = 0;
    uint8_t tmod = 0;
    uint8_t tl0 = 0;
    uint8_t tl1 = 0;
    uint8_t th0 = 0;
    uint8_t th1 = 0;
    uint8_t scon = 0;
    uint8_t sbuf = 0;
    std::array<uint8_t, 256> iram{};
    bool halted = false;
    uint8_t halted_opcode = 0;
    uint8_t irq_nesting = 0;
    uint64_t executed = 0;
    uint64_t unsupported_count = 0;
};

class Dw8051Sfr {
public:
    void Reset();

    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);

    void AdvanceTimers();
    bool ServiceInterrupts(bool external_interrupt0_pending);
    void SignalExternalInterrupt0() { state_.tcon |= 0x02u; }
    void ClearExternalInterrupt0() { state_.tcon &= static_cast<uint8_t>(~0x02u); }

    uint8_t ReadDirect(uint8_t address) const;
    void WriteDirect(uint8_t address, uint8_t value);
    uint8_t ReadBit(uint8_t bit_address) const;
    void WriteBit(uint8_t bit_address, bool value);
    uint8_t ReadIndirect(uint8_t address) const;
    void WriteIndirect(uint8_t address, uint8_t value);

    uint8_t Accumulator() const;
    void SetAccumulator(uint8_t value);
    uint8_t Register(uint8_t index) const;
    void SetRegister(uint8_t index, uint8_t value);
    bool Carry() const;
    void SetCarry(bool value);
    uint16_t Dptr() const;
    void SetDptr(uint16_t value);
    void Push(uint8_t value);
    uint8_t Pop();

    uint16_t Pc() const { return state_.pc; }
    void SetPc(uint16_t value) { state_.pc = value; }
    uint16_t FetchPc() { return state_.pc++; }
    uint8_t B() const { return state_.b; }
    void SetB(uint8_t value) { state_.b = value; }
    uint8_t Psw() const { return state_.psw; }
    void SetPsw(uint8_t value) { state_.psw = value; }
    uint8_t Port2() const { return state_.p2; }

    bool Halted() const { return state_.halted; }
    void Halt(uint8_t opcode);
    uint64_t Executed() const { return state_.executed; }
    void BumpExecuted() { ++state_.executed; }
    uint64_t UnsupportedCount() const { return state_.unsupported_count; }
    uint8_t IrqNesting() const { return state_.irq_nesting; }
    void LeaveInterrupt();

private:
    void EnterInterrupt(uint16_t vector);
    [[noreturn]] void UnsupportedSfr(uint8_t address, bool write, uint8_t value) const;

    Dw8051State state_;
};
