#pragma once

#include "dw8051_sfr.h"

#include <cstdint>

/* Intel MCS-51 User's Manual, February 1994, Chapter 2: program and data
   address spaces are separate; MOVX reaches external data through DPTR/Ri,
   while direct/indirect accesses target internal RAM and SFRs. */
class Dw8051Bus {
public:
    virtual ~Dw8051Bus() = default;
    virtual uint8_t FetchCode(uint16_t address) const = 0;
    virtual uint8_t ReadExternal(uint16_t address) = 0;
    virtual void WriteExternal(uint16_t address, uint8_t value) = 0;
};

class Dw8051Core {
public:
    void Reset() { sfr_.Reset(); }
    bool RunInstruction(Dw8051Bus& bus, bool external_interrupt0_pending);

    bool Halted() const { return sfr_.Halted(); }
    uint16_t ProgramCounter() const { return sfr_.Pc(); }
    void SignalExternalInterrupt0() { sfr_.SignalExternalInterrupt0(); }
    void ClearExternalInterrupt0() { sfr_.ClearExternalInterrupt0(); }

    void SaveState(StateWriter& writer) const { sfr_.SaveState(writer); }
    void RestoreState(StateReader& reader) { sfr_.RestoreState(reader); }

private:
    bool Step(Dw8051Bus& bus);
    void Call(uint16_t target);
    void Return();
    [[noreturn]] void Unsupported(uint8_t opcode, uint16_t pc);
    uint16_t AjmpTarget(uint8_t opcode, uint8_t low) const;
    void Add(uint8_t value, bool with_carry);
    void Subb(uint8_t value);
    void Compare(uint8_t lhs, uint8_t rhs);

    Dw8051Sfr sfr_;
};
