#include "dw8051_sfr.h"

#include "../../core/log.h"

void Dw8051Sfr::Reset() {
    state_ = Dw8051State{};
}

void Dw8051Sfr::AdvanceTimers() {
    auto advance = [this](bool timer1) {
        const uint8_t tr = timer1 ? 0x40u : 0x10u;
        const uint8_t tf = timer1 ? 0x80u : 0x20u;
        if ((state_.tcon & tr) == 0u) return;
        const uint8_t mode =
            timer1 ? static_cast<uint8_t>((state_.tmod >> 4) & 3u) : static_cast<uint8_t>(state_.tmod & 3u);
        uint8_t& tl = timer1 ? state_.tl1 : state_.tl0;
        uint8_t& th = timer1 ? state_.th1 : state_.th0;
        switch (mode) {
        case 0: {
            uint16_t value = static_cast<uint16_t>((static_cast<uint16_t>(th) << 5) | (tl & 0x1Fu));
            value = static_cast<uint16_t>((value + 1u) & 0x1FFFu);
            if (value == 0u) state_.tcon |= tf;
            th = static_cast<uint8_t>(value >> 5);
            tl = static_cast<uint8_t>((tl & 0xE0u) | (value & 0x1Fu));
            break;
        }
        case 1: {
            uint16_t value = static_cast<uint16_t>((static_cast<uint16_t>(th) << 8) | tl);
            value = static_cast<uint16_t>(value + 1u);
            if (value == 0u) state_.tcon |= tf;
            th = static_cast<uint8_t>(value >> 8);
            tl = static_cast<uint8_t>(value);
            break;
        }
        case 2:
            tl = static_cast<uint8_t>(tl + 1u);
            if (tl == 0u) {
                state_.tcon |= tf;
                tl = th;
            }
            break;
        default: break;
        }
    };
    advance(false);
    advance(true);
}

bool Dw8051Sfr::ServiceInterrupts(bool external_interrupt0_pending) {
    if (state_.irq_nesting != 0u || (state_.ie & 0x80u) == 0u) return false;
    if (external_interrupt0_pending) {
        SignalExternalInterrupt0();
        if ((state_.ie & 0x01u) != 0u) {
            EnterInterrupt(0x0003u);
            return true;
        }
    }
    if ((state_.tcon & 0x20u) != 0u && (state_.ie & 0x02u) != 0u) {
        state_.tcon &= static_cast<uint8_t>(~0x20u);
        EnterInterrupt(0x000Bu);
        return true;
    }
    if ((state_.tcon & 0x80u) != 0u && (state_.ie & 0x08u) != 0u) {
        state_.tcon &= static_cast<uint8_t>(~0x80u);
        EnterInterrupt(0x001Bu);
        return true;
    }
    if ((state_.ie & 0x10u) != 0u && (state_.scon & 0x03u) != 0u) {
        EnterInterrupt(0x0023u);
        return true;
    }
    return false;
}

void Dw8051Sfr::EnterInterrupt(uint16_t vector) {
    Push(static_cast<uint8_t>(state_.pc));
    Push(static_cast<uint8_t>(state_.pc >> 8));
    state_.pc = vector;
    state_.irq_nesting = 1u;
}

uint8_t Dw8051Sfr::ReadDirect(uint8_t address) const {
    if (address < 0x80u) return state_.iram[address];
    switch (address) {
    case 0x80u: return state_.p0;
    case 0x81u: return state_.sp;
    case 0x82u: return state_.dpl;
    case 0x83u: return state_.dph;
    case 0x88u: return state_.tcon;
    case 0x89u: return state_.tmod;
    case 0x8Au: return state_.tl0;
    case 0x8Bu: return state_.tl1;
    case 0x8Cu: return state_.th0;
    case 0x8Du: return state_.th1;
    case 0x90u: return state_.p1;
    case 0x98u: return state_.scon;
    case 0x99u: return state_.sbuf;
    case 0xA0u: return state_.p2;
    case 0xA8u: return state_.ie;
    case 0xB0u: return state_.p3;
    case 0xB8u: return state_.ip;
    case 0xD0u: return state_.psw;
    case 0xE0u: return state_.acc;
    /* DW8051 manual, Table 31: EIE, SFR E8h — EX2/EX3/EX4/EX5/EWDI enables;
       bits 7:5 reserved, read as 1.  The MP377 audio firmware writes 0x06
       (EX3|EX4) from the extended vector at 0x33. */
    case 0xE8u: return state_.eie;
    /* DW8051 manual, Table 32: EIP, SFR F8h — PX2/PX3/PX4/PX5 priority
       controls; same reserved-as-1 pattern as EIE. */
    case 0xF8u: return state_.eip;
    case 0xF0u: return state_.b;
    }
    UnsupportedSfr(address, /*write=*/false, 0u);
}

void Dw8051Sfr::WriteDirect(uint8_t address, uint8_t value) {
    if (address < 0x80u) {
        state_.iram[address] = value;
        return;
    }
    switch (address) {
    case 0x80u: state_.p0 = value; break;
    case 0x81u: state_.sp = value; break;
    case 0x82u: state_.dpl = value; break;
    case 0x83u: state_.dph = value; break;
    case 0x88u: state_.tcon = value; break;
    case 0x89u: state_.tmod = value; break;
    case 0x8Au: state_.tl0 = value; break;
    case 0x8Bu: state_.tl1 = value; break;
    case 0x8Cu: state_.th0 = value; break;
    case 0x8Du: state_.th1 = value; break;
    case 0x90u: state_.p1 = value; break;
    case 0x98u: state_.scon = value; break;
    case 0x99u: state_.sbuf = value; break;
    case 0xA0u: state_.p2 = value; break;
    case 0xA8u: state_.ie = value; break;
    case 0xB0u: state_.p3 = value; break;
    case 0xB8u: state_.ip = value; break;
    case 0xD0u: state_.psw = value; break;
    case 0xE0u: SetAccumulator(value); break;
    /* DW8051 manual, Table 31: EIE, SFR E8h — writable enable bits 4:0,
       reserved 7:5 stay 1. */
    case 0xE8u: state_.eie = static_cast<uint8_t>((value & 0x1Fu) | 0xE0u); break;
    /* DW8051 manual, Table 32: EIP, SFR F8h — writable priority bits 3:0
       (+EWDI-class bit 4), reserved 7:5 stay 1. */
    case 0xF8u: state_.eip = static_cast<uint8_t>((value & 0x1Fu) | 0xE0u); break;
    case 0xF0u: state_.b = value; break;
    default: UnsupportedSfr(address, /*write=*/true, value);
    }
}

uint8_t Dw8051Sfr::ReadBit(uint8_t bit_address) const {
    const uint8_t byte_address = bit_address < 0x80u ? static_cast<uint8_t>(0x20u + (bit_address >> 3))
                                                     : static_cast<uint8_t>(bit_address & 0xF8u);
    return static_cast<uint8_t>((ReadDirect(byte_address) >> (bit_address & 7u)) & 1u);
}

void Dw8051Sfr::WriteBit(uint8_t bit_address, bool value) {
    const uint8_t byte_address = bit_address < 0x80u ? static_cast<uint8_t>(0x20u + (bit_address >> 3))
                                                     : static_cast<uint8_t>(bit_address & 0xF8u);
    uint8_t byte = ReadDirect(byte_address);
    const uint8_t mask = static_cast<uint8_t>(1u << (bit_address & 7u));
    byte = value ? static_cast<uint8_t>(byte | mask) : static_cast<uint8_t>(byte & ~mask);
    WriteDirect(byte_address, byte);
}

uint8_t Dw8051Sfr::ReadIndirect(uint8_t address) const {
    return state_.iram[address];
}

void Dw8051Sfr::WriteIndirect(uint8_t address, uint8_t value) {
    state_.iram[address] = value;
}

uint8_t Dw8051Sfr::Accumulator() const {
    return state_.acc;
}

void Dw8051Sfr::SetAccumulator(uint8_t value) {
    state_.acc = value;
    uint8_t parity = value;
    parity ^= static_cast<uint8_t>(parity >> 4);
    parity ^= static_cast<uint8_t>(parity >> 2);
    parity ^= static_cast<uint8_t>(parity >> 1);
    if ((parity & 1u) != 0u)
        state_.psw |= 0x01u;
    else
        state_.psw &= static_cast<uint8_t>(~0x01u);
}

uint8_t Dw8051Sfr::Register(uint8_t index) const {
    const uint8_t bank = static_cast<uint8_t>((state_.psw >> 3) & 0x03u);
    return state_.iram[static_cast<uint8_t>(bank * 8u + (index & 7u))];
}

void Dw8051Sfr::SetRegister(uint8_t index, uint8_t value) {
    const uint8_t bank = static_cast<uint8_t>((state_.psw >> 3) & 0x03u);
    state_.iram[static_cast<uint8_t>(bank * 8u + (index & 7u))] = value;
}

bool Dw8051Sfr::Carry() const {
    return (state_.psw & 0x80u) != 0u;
}

void Dw8051Sfr::SetCarry(bool value) {
    if (value)
        state_.psw |= 0x80u;
    else
        state_.psw &= static_cast<uint8_t>(~0x80u);
}

uint16_t Dw8051Sfr::Dptr() const {
    return static_cast<uint16_t>((static_cast<uint16_t>(state_.dph) << 8) | state_.dpl);
}

void Dw8051Sfr::SetDptr(uint16_t value) {
    state_.dpl = static_cast<uint8_t>(value);
    state_.dph = static_cast<uint8_t>(value >> 8);
}

void Dw8051Sfr::Push(uint8_t value) {
    state_.sp = static_cast<uint8_t>(state_.sp + 1u);
    WriteIndirect(state_.sp, value);
}

uint8_t Dw8051Sfr::Pop() {
    const uint8_t value = ReadIndirect(state_.sp);
    state_.sp = static_cast<uint8_t>(state_.sp - 1u);
    return value;
}

[[noreturn]] void Dw8051Sfr::UnsupportedSfr(uint8_t address, bool write, uint8_t value) const {
    /* Intel MCS-51 User's Manual ch. 2: direct addresses 0x80-0xFF are the
       SFR space.  An SFR this core does not model is a peripheral the
       firmware talks to, so it halts here instead of answering. */
    LOG(Caution,
        "MCS-51: unmodelled SFR %s address=0x%02X value=0x%02X pc=0x%04X "
        "executed=%llu\n",
        write ? "write" : "read", address, value, state_.pc, static_cast<unsigned long long>(state_.executed));
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void Dw8051Sfr::Halt(uint8_t opcode) {
    state_.halted = true;
    state_.halted_opcode = opcode;
    ++state_.unsupported_count;
}

void Dw8051Sfr::LeaveInterrupt() {
    if (state_.irq_nesting != 0u) --state_.irq_nesting;
}

void Dw8051Sfr::SaveState(StateWriter& w) const {
    w.Write(state_.pc);
    w.Write(state_.sp);
    w.Write(state_.acc);
    w.Write(state_.b);
    w.Write(state_.psw);
    w.Write(state_.dpl);
    w.Write(state_.dph);
    w.Write(state_.p0);
    w.Write(state_.p1);
    w.Write(state_.p2);
    w.Write(state_.p3);
    w.Write(state_.ie);
    w.Write(state_.eie);
    w.Write(state_.eip);
    w.Write(state_.ip);
    w.Write(state_.tcon);
    w.Write(state_.tmod);
    w.Write(state_.tl0);
    w.Write(state_.tl1);
    w.Write(state_.th0);
    w.Write(state_.th1);
    w.Write(state_.scon);
    w.Write(state_.sbuf);
    w.WriteBytes(state_.iram.data(), state_.iram.size());
    w.Write(state_.halted);
    w.Write(state_.halted_opcode);
    w.Write(state_.irq_nesting);
    w.Write(state_.executed);
    w.Write(state_.unsupported_count);
}

void Dw8051Sfr::RestoreState(StateReader& r) {
    r.Read(state_.pc);
    r.Read(state_.sp);
    r.Read(state_.acc);
    r.Read(state_.b);
    r.Read(state_.psw);
    r.Read(state_.dpl);
    r.Read(state_.dph);
    r.Read(state_.p0);
    r.Read(state_.p1);
    r.Read(state_.p2);
    r.Read(state_.p3);
    r.Read(state_.ie);
    r.Read(state_.eie);
    r.Read(state_.eip);
    r.Read(state_.ip);
    r.Read(state_.tcon);
    r.Read(state_.tmod);
    r.Read(state_.tl0);
    r.Read(state_.tl1);
    r.Read(state_.th0);
    r.Read(state_.th1);
    r.Read(state_.scon);
    r.Read(state_.sbuf);
    r.ReadBytes(state_.iram.data(), state_.iram.size());
    r.Read(state_.halted);
    r.Read(state_.halted_opcode);
    r.Read(state_.irq_nesting);
    r.Read(state_.executed);
    r.Read(state_.unsupported_count);
}
