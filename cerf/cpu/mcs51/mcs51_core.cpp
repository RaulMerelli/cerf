#include "mcs51_core.h"

#include "../../core/log.h"

void Mcs51Core::Reset() {
    state_ = State{};
}

bool Mcs51Core::RunInstruction(Mcs51Bus& bus,
                               bool external_interrupt0_pending) {
    if (state_.halted) return false;
    ServiceInterrupts(external_interrupt0_pending);
    if (!Step(bus)) return false;
    AdvanceTimers();
    ServiceInterrupts(external_interrupt0_pending);
    return true;
}

void Mcs51Core::SaveState(StateWriter& w) const {
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

void Mcs51Core::RestoreState(StateReader& r) {
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

void Mcs51Core::AdvanceTimers() {
    auto advance = [this](bool timer1) {
        const uint8_t tr = timer1 ? 0x40u : 0x10u;
        const uint8_t tf = timer1 ? 0x80u : 0x20u;
        if ((state_.tcon & tr) == 0u) return;
        const uint8_t mode = timer1
            ? static_cast<uint8_t>((state_.tmod >> 4) & 3u)
            : static_cast<uint8_t>(state_.tmod & 3u);
        uint8_t& tl = timer1 ? state_.tl1 : state_.tl0;
        uint8_t& th = timer1 ? state_.th1 : state_.th0;
        switch (mode) {
        case 0: {
            uint16_t value = static_cast<uint16_t>(
                (static_cast<uint16_t>(th) << 5) | (tl & 0x1Fu));
            value = static_cast<uint16_t>((value + 1u) & 0x1FFFu);
            if (value == 0u) state_.tcon |= tf;
            th = static_cast<uint8_t>(value >> 5);
            tl = static_cast<uint8_t>((tl & 0xE0u) | (value & 0x1Fu));
            break;
        }
        case 1: {
            uint16_t value = static_cast<uint16_t>(
                (static_cast<uint16_t>(th) << 8) | tl);
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
        default:
            break;
        }
    };
    advance(false);
    advance(true);
}

bool Mcs51Core::ServiceInterrupts(bool external_interrupt0_pending) {
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

void Mcs51Core::EnterInterrupt(uint16_t vector) {
    Push(static_cast<uint8_t>(state_.pc));
    Push(static_cast<uint8_t>(state_.pc >> 8));
    state_.pc = vector;
    state_.irq_nesting = 1u;
}

uint8_t Mcs51Core::ReadDirect(uint8_t address) const {
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
    case 0xF0u: return state_.b;
    default: return 0u;
    }
}

void Mcs51Core::WriteDirect(uint8_t address, uint8_t value) {
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
    case 0xF0u: state_.b = value; break;
    default: break;
    }
}

uint8_t Mcs51Core::ReadBit(uint8_t bit_address) const {
    const uint8_t byte_address = bit_address < 0x80u
        ? static_cast<uint8_t>(0x20u + (bit_address >> 3))
        : static_cast<uint8_t>(bit_address & 0xF8u);
    return static_cast<uint8_t>(
        (ReadDirect(byte_address) >> (bit_address & 7u)) & 1u);
}

void Mcs51Core::WriteBit(uint8_t bit_address, bool value) {
    const uint8_t byte_address = bit_address < 0x80u
        ? static_cast<uint8_t>(0x20u + (bit_address >> 3))
        : static_cast<uint8_t>(bit_address & 0xF8u);
    uint8_t byte = ReadDirect(byte_address);
    const uint8_t mask = static_cast<uint8_t>(1u << (bit_address & 7u));
    byte = value ? static_cast<uint8_t>(byte | mask)
                 : static_cast<uint8_t>(byte & ~mask);
    WriteDirect(byte_address, byte);
}

uint8_t Mcs51Core::ReadIndirect(uint8_t address) const {
    return state_.iram[address];
}

void Mcs51Core::WriteIndirect(uint8_t address, uint8_t value) {
    state_.iram[address] = value;
}

uint8_t Mcs51Core::Accumulator() const { return state_.acc; }

void Mcs51Core::SetAccumulator(uint8_t value) {
    state_.acc = value;
    uint8_t parity = value;
    parity ^= static_cast<uint8_t>(parity >> 4);
    parity ^= static_cast<uint8_t>(parity >> 2);
    parity ^= static_cast<uint8_t>(parity >> 1);
    if ((parity & 1u) != 0u) state_.psw |= 0x01u;
    else state_.psw &= static_cast<uint8_t>(~0x01u);
}

uint8_t Mcs51Core::Register(uint8_t index) const {
    const uint8_t bank = static_cast<uint8_t>((state_.psw >> 3) & 0x03u);
    return state_.iram[static_cast<uint8_t>(bank * 8u + (index & 7u))];
}

void Mcs51Core::SetRegister(uint8_t index, uint8_t value) {
    const uint8_t bank = static_cast<uint8_t>((state_.psw >> 3) & 0x03u);
    state_.iram[static_cast<uint8_t>(bank * 8u + (index & 7u))] = value;
}

bool Mcs51Core::Carry() const { return (state_.psw & 0x80u) != 0u; }

void Mcs51Core::SetCarry(bool value) {
    if (value) state_.psw |= 0x80u;
    else state_.psw &= static_cast<uint8_t>(~0x80u);
}

uint16_t Mcs51Core::Dptr() const {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(state_.dph) << 8) | state_.dpl);
}

void Mcs51Core::SetDptr(uint16_t value) {
    state_.dpl = static_cast<uint8_t>(value);
    state_.dph = static_cast<uint8_t>(value >> 8);
}

void Mcs51Core::Push(uint8_t value) {
    state_.sp = static_cast<uint8_t>(state_.sp + 1u);
    WriteIndirect(state_.sp, value);
}

uint8_t Mcs51Core::Pop() {
    const uint8_t value = ReadIndirect(state_.sp);
    state_.sp = static_cast<uint8_t>(state_.sp - 1u);
    return value;
}

void Mcs51Core::Call(uint16_t target) {
    const uint16_t ret = state_.pc;
    Push(static_cast<uint8_t>(ret));
    Push(static_cast<uint8_t>(ret >> 8));
    state_.pc = target;
}

void Mcs51Core::Return() {
    const uint8_t high = Pop();
    const uint8_t low = Pop();
    state_.pc = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
}

[[noreturn]] void Mcs51Core::Unsupported(uint8_t opcode, uint16_t pc) {
    state_.halted = true;
    state_.halted_opcode = opcode;
    ++state_.unsupported_count;
    LOG(Caution,
        "MCS-51: unsupported opcode=0x%02X pc=0x%04X executed=%llu unsupported=%llu\n",
        opcode, pc, static_cast<unsigned long long>(state_.executed),
        static_cast<unsigned long long>(state_.unsupported_count));
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

uint16_t Mcs51Core::AjmpTarget(uint8_t opcode, uint8_t low) const {
    return static_cast<uint16_t>((state_.pc & 0xF800u) |
        (static_cast<uint16_t>(opcode & 0xE0u) << 3) | low);
}

void Mcs51Core::Add(uint8_t value, bool with_carry) {
    const uint8_t a = Accumulator();
    const uint8_t carry = (with_carry && Carry()) ? 1u : 0u;
    const uint16_t sum = static_cast<uint16_t>(a) + value + carry;
    uint8_t psw = static_cast<uint8_t>(state_.psw & ~(0x80u | 0x40u | 0x04u));
    if (sum > 0xFFu) psw |= 0x80u;
    if (((a & 0x0Fu) + (value & 0x0Fu) + carry) > 0x0Fu) psw |= 0x40u;
    const uint8_t result = static_cast<uint8_t>(sum);
    if (((~(a ^ value)) & (a ^ result) & 0x80u) != 0u) psw |= 0x04u;
    state_.psw = psw;
    SetAccumulator(result);
}

void Mcs51Core::Subb(uint8_t value) {
    const uint8_t a = Accumulator();
    const uint8_t carry = Carry() ? 1u : 0u;
    const uint16_t rhs = static_cast<uint16_t>(value) + carry;
    const uint16_t difference = static_cast<uint16_t>(a) - rhs;
    uint8_t psw = static_cast<uint8_t>(state_.psw & ~(0x80u | 0x40u | 0x04u));
    if (static_cast<uint16_t>(a) < rhs) psw |= 0x80u;
    if ((a & 0x0Fu) < ((value & 0x0Fu) + carry)) psw |= 0x40u;
    const uint8_t result = static_cast<uint8_t>(difference);
    if (((a ^ value) & (a ^ result) & 0x80u) != 0u) psw |= 0x04u;
    state_.psw = psw;
    SetAccumulator(result);
}

void Mcs51Core::Compare(uint8_t lhs, uint8_t rhs) {
    SetCarry(lhs < rhs);
}


bool Mcs51Core::Step(Mcs51Bus& bus) {
    if (state_.halted) return false;
    const uint16_t pc0 = state_.pc;
    const uint8_t op = bus.FetchCode(state_.pc++);
    ++state_.executed;

    auto imm8 = [this, &bus]() -> uint8_t { return bus.FetchCode(state_.pc++); };
    auto imm16 = [&]() -> uint16_t {
        const uint8_t high = imm8();
        const uint8_t low = imm8();
        return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
    };
    auto rel8 = [&]() -> int8_t { return static_cast<int8_t>(imm8()); };
    auto branch = [&](int8_t rel) { state_.pc = static_cast<uint16_t>(state_.pc + rel); };
    auto indir_addr = [&](uint8_t r) -> uint8_t { return Register(r); };
    auto movx_ri_addr = [&](uint8_t r) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(state_.p2) << 8) | Register(r));
    };

    if ((op & 0x1Fu) == 0x01u) { const uint8_t low = imm8(); state_.pc = AjmpTarget(op, low); return true; }
    if ((op & 0x1Fu) == 0x11u) { const uint8_t low = imm8(); Call(AjmpTarget(op, low)); return true; }
    if (op >= 0x08u && op <= 0x0Fu) { const uint8_t r = op & 7u; SetRegister(r, static_cast<uint8_t>(Register(r) + 1u)); return true; }
    if (op >= 0x18u && op <= 0x1Fu) { const uint8_t r = op & 7u; SetRegister(r, static_cast<uint8_t>(Register(r) - 1u)); return true; }
    if (op >= 0x28u && op <= 0x2Fu) { Add(Register(op & 7u), false); return true; }
    if (op >= 0x38u && op <= 0x3Fu) { Add(Register(op & 7u), true); return true; }
    if (op >= 0x48u && op <= 0x4Fu) { SetAccumulator(static_cast<uint8_t>(Accumulator() | Register(op & 7u))); return true; }
    if (op >= 0x58u && op <= 0x5Fu) { SetAccumulator(static_cast<uint8_t>(Accumulator() & Register(op & 7u))); return true; }
    if (op >= 0x68u && op <= 0x6Fu) { SetAccumulator(static_cast<uint8_t>(Accumulator() ^ Register(op & 7u))); return true; }
    if (op >= 0x78u && op <= 0x7Fu) { SetRegister(op & 7u, imm8()); return true; }
    if (op >= 0x88u && op <= 0x8Fu) { WriteDirect(imm8(), Register(op & 7u)); return true; }
    if (op >= 0x98u && op <= 0x9Fu) { Subb(Register(op & 7u)); return true; }
    if (op >= 0xA8u && op <= 0xAFu) { SetRegister(op & 7u, ReadDirect(imm8())); return true; }
    if (op >= 0xB8u && op <= 0xBFu) { const uint8_t rhs = imm8(); const int8_t rel = rel8(); const uint8_t lhs = Register(op & 7u); Compare(lhs, rhs); if (lhs != rhs) branch(rel); return true; }
    if (op >= 0xC8u && op <= 0xCFu) { const uint8_t r = op & 7u; const uint8_t tmp = Accumulator(); SetAccumulator(Register(r)); SetRegister(r, tmp); return true; }
    if (op >= 0xD8u && op <= 0xDFu) { const uint8_t r = op & 7u; const int8_t rel = rel8(); const uint8_t v = static_cast<uint8_t>(Register(r) - 1u); SetRegister(r, v); if (v != 0u) branch(rel); return true; }
    if (op >= 0xE8u && op <= 0xEFu) { SetAccumulator(Register(op & 7u)); return true; }
    if (op >= 0xF8u && op <= 0xFFu) { SetRegister(op & 7u, Accumulator()); return true; }

    switch (op) {
    case 0x00u: return true;
    case 0x02u: state_.pc = imm16(); return true;
    case 0x03u: { const uint8_t a = Accumulator(); SetAccumulator(static_cast<uint8_t>((a >> 1) | (a << 7))); return true; }
    case 0x04u: SetAccumulator(static_cast<uint8_t>(Accumulator() + 1u)); return true;
    case 0x05u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) + 1u)); return true; }
    case 0x06u: case 0x07u: { const uint8_t a = indir_addr(op & 1u); WriteIndirect(a, static_cast<uint8_t>(ReadIndirect(a) + 1u)); return true; }
    case 0x10u: { const uint8_t b = imm8(); const int8_t r = rel8(); if (ReadBit(b)) { WriteBit(b, false); branch(r); } return true; }
    case 0x12u: Call(imm16()); return true;
    case 0x13u: { const uint8_t a = Accumulator(); const bool c = Carry(); SetCarry((a & 1u) != 0u); SetAccumulator(static_cast<uint8_t>((a >> 1) | (c ? 0x80u : 0u))); return true; }
    case 0x14u: SetAccumulator(static_cast<uint8_t>(Accumulator() - 1u)); return true;
    case 0x15u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) - 1u)); return true; }
    case 0x16u: case 0x17u: { const uint8_t a = indir_addr(op & 1u); WriteIndirect(a, static_cast<uint8_t>(ReadIndirect(a) - 1u)); return true; }
    case 0x20u: { const uint8_t b = imm8(); const int8_t r = rel8(); if (ReadBit(b)) branch(r); return true; }
    case 0x22u: Return(); return true;
    case 0x23u: { const uint8_t a = Accumulator(); SetAccumulator(static_cast<uint8_t>((a << 1) | (a >> 7))); return true; }
    case 0x24u: Add(imm8(), false); return true;
    case 0x25u: Add(ReadDirect(imm8()), false); return true;
    case 0x26u: case 0x27u: Add(ReadIndirect(indir_addr(op & 1u)), false); return true;
    case 0x30u: { const uint8_t b = imm8(); const int8_t r = rel8(); if (!ReadBit(b)) branch(r); return true; }
    case 0x32u: Return(); if (state_.irq_nesting != 0u) --state_.irq_nesting; return true;
    case 0x33u: { const uint8_t a = Accumulator(); const bool c = Carry(); SetCarry((a & 0x80u) != 0u); SetAccumulator(static_cast<uint8_t>((a << 1) | (c ? 1u : 0u))); return true; }
    case 0x34u: Add(imm8(), true); return true;
    case 0x35u: Add(ReadDirect(imm8()), true); return true;
    case 0x36u: case 0x37u: Add(ReadIndirect(indir_addr(op & 1u)), true); return true;
    case 0x40u: { const int8_t r = rel8(); if (Carry()) branch(r); return true; }
    case 0x42u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) | Accumulator())); return true; }
    case 0x43u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) | imm8())); return true; }
    case 0x44u: SetAccumulator(static_cast<uint8_t>(Accumulator() | imm8())); return true;
    case 0x45u: SetAccumulator(static_cast<uint8_t>(Accumulator() | ReadDirect(imm8()))); return true;
    case 0x46u: case 0x47u: SetAccumulator(static_cast<uint8_t>(Accumulator() | ReadIndirect(indir_addr(op & 1u)))); return true;
    case 0x50u: { const int8_t r = rel8(); if (!Carry()) branch(r); return true; }
    case 0x52u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) & Accumulator())); return true; }
    case 0x53u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) & imm8())); return true; }
    case 0x54u: SetAccumulator(static_cast<uint8_t>(Accumulator() & imm8())); return true;
    case 0x55u: SetAccumulator(static_cast<uint8_t>(Accumulator() & ReadDirect(imm8()))); return true;
    case 0x56u: case 0x57u: SetAccumulator(static_cast<uint8_t>(Accumulator() & ReadIndirect(indir_addr(op & 1u)))); return true;
    case 0x60u: { const int8_t r = rel8(); if (Accumulator() == 0u) branch(r); return true; }
    case 0x62u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) ^ Accumulator())); return true; }
    case 0x63u: { const uint8_t d = imm8(); WriteDirect(d, static_cast<uint8_t>(ReadDirect(d) ^ imm8())); return true; }
    case 0x64u: SetAccumulator(static_cast<uint8_t>(Accumulator() ^ imm8())); return true;
    case 0x65u: SetAccumulator(static_cast<uint8_t>(Accumulator() ^ ReadDirect(imm8()))); return true;
    case 0x66u: case 0x67u: SetAccumulator(static_cast<uint8_t>(Accumulator() ^ ReadIndirect(indir_addr(op & 1u)))); return true;
    case 0x70u: { const int8_t r = rel8(); if (Accumulator() != 0u) branch(r); return true; }
    case 0x72u: SetCarry(Carry() || ReadBit(imm8())); return true;
    case 0x73u: state_.pc = static_cast<uint16_t>(Dptr() + Accumulator()); return true;
    case 0x74u: SetAccumulator(imm8()); return true;
    case 0x75u: { const uint8_t d = imm8(); WriteDirect(d, imm8()); return true; }
    case 0x76u: case 0x77u: WriteIndirect(indir_addr(op & 1u), imm8()); return true;
    case 0x80u: branch(rel8()); return true;
    case 0x82u: SetCarry(Carry() && ReadBit(imm8())); return true;
    case 0x83u: { const uint16_t addr = static_cast<uint16_t>(state_.pc + Accumulator()); SetAccumulator(bus.FetchCode(addr)); return true; }
    case 0x84u: { const uint8_t divisor = state_.b; const uint16_t q = divisor ? (Accumulator() / divisor) : 0u; const uint16_t r = divisor ? (Accumulator() % divisor) : 0u; state_.psw &= static_cast<uint8_t>(~0x80u); if (divisor == 0u) state_.psw |= 0x04u; else state_.psw &= static_cast<uint8_t>(~0x04u); SetAccumulator(static_cast<uint8_t>(q)); state_.b = static_cast<uint8_t>(r); return true; }
    case 0x85u: { const uint8_t src = imm8(); const uint8_t dst = imm8(); WriteDirect(dst, ReadDirect(src)); return true; }
    case 0x86u: case 0x87u: WriteDirect(imm8(), ReadIndirect(indir_addr(op & 1u))); return true;
    case 0x90u: SetDptr(imm16()); return true;
    case 0x92u: WriteBit(imm8(), Carry()); return true;
    case 0x93u: SetAccumulator(bus.FetchCode(static_cast<uint16_t>(Dptr() + Accumulator()))); return true;
    case 0x94u: Subb(imm8()); return true;
    case 0x95u: Subb(ReadDirect(imm8())); return true;
    case 0x96u: case 0x97u: Subb(ReadIndirect(indir_addr(op & 1u))); return true;
    case 0xA0u: SetCarry(Carry() || !ReadBit(imm8())); return true;
    case 0xA2u: SetCarry(ReadBit(imm8()) != 0u); return true;
    case 0xA3u: SetDptr(static_cast<uint16_t>(Dptr() + 1u)); return true;
    case 0xA4u: { const uint16_t prod = static_cast<uint16_t>(Accumulator()) * state_.b; SetAccumulator(static_cast<uint8_t>(prod)); state_.b = static_cast<uint8_t>(prod >> 8); state_.psw &= static_cast<uint8_t>(~0x80u); if (state_.b) state_.psw |= 0x04u; else state_.psw &= static_cast<uint8_t>(~0x04u); return true; }
    case 0xA6u: case 0xA7u: WriteIndirect(indir_addr(op & 1u), ReadDirect(imm8())); return true;
    case 0xB0u: SetCarry(Carry() && !ReadBit(imm8())); return true;
    case 0xB2u: { const uint8_t b = imm8(); WriteBit(b, !ReadBit(b)); return true; }
    case 0xB3u: SetCarry(!Carry()); return true;
    case 0xB4u: { const uint8_t rhs = imm8(); const int8_t r = rel8(); const uint8_t lhs = Accumulator(); Compare(lhs, rhs); if (lhs != rhs) branch(r); return true; }
    case 0xB5u: { const uint8_t rhs = ReadDirect(imm8()); const int8_t r = rel8(); const uint8_t lhs = Accumulator(); Compare(lhs, rhs); if (lhs != rhs) branch(r); return true; }
    case 0xB6u: case 0xB7u: { const uint8_t lhs = ReadIndirect(indir_addr(op & 1u)); const uint8_t rhs = imm8(); const int8_t r = rel8(); Compare(lhs, rhs); if (lhs != rhs) branch(r); return true; }
    case 0xC0u: Push(ReadDirect(imm8())); return true;
    case 0xC2u: WriteBit(imm8(), false); return true;
    case 0xC3u: SetCarry(false); return true;
    case 0xC4u: { const uint8_t a = Accumulator(); SetAccumulator(static_cast<uint8_t>((a << 4) | (a >> 4))); return true; }
    case 0xC5u: { const uint8_t d = imm8(); const uint8_t tmp = Accumulator(); SetAccumulator(ReadDirect(d)); WriteDirect(d, tmp); return true; }
    case 0xC6u: case 0xC7u: { const uint8_t a = indir_addr(op & 1u); const uint8_t tmp = Accumulator(); SetAccumulator(ReadIndirect(a)); WriteIndirect(a, tmp); return true; }
    case 0xD0u: WriteDirect(imm8(), Pop()); return true;
    case 0xD2u: WriteBit(imm8(), true); return true;
    case 0xD3u: SetCarry(true); return true;
    case 0xD4u: { uint8_t a = Accumulator(); bool c = Carry(); if ((a & 0x0Fu) > 9u || (state_.psw & 0x40u)) a = static_cast<uint8_t>(a + 0x06u); if ((a & 0xF0u) > 0x90u || c) { a = static_cast<uint8_t>(a + 0x60u); c = true; } SetAccumulator(a); SetCarry(c); return true; }
    case 0xD5u: { const uint8_t d = imm8(); const int8_t r = rel8(); const uint8_t v = static_cast<uint8_t>(ReadDirect(d) - 1u); WriteDirect(d, v); if (v != 0u) branch(r); return true; }
    case 0xD6u: case 0xD7u: { const uint8_t a = indir_addr(op & 1u); const uint8_t mem = ReadIndirect(a); const uint8_t acc = Accumulator(); WriteIndirect(a, static_cast<uint8_t>((mem & 0xF0u) | (acc & 0x0Fu))); SetAccumulator(static_cast<uint8_t>((acc & 0xF0u) | (mem & 0x0Fu))); return true; }
    case 0xE0u: SetAccumulator(bus.ReadExternal(Dptr())); return true;
    case 0xE2u: case 0xE3u: SetAccumulator(bus.ReadExternal(movx_ri_addr(op & 1u))); return true;
    case 0xE4u: SetAccumulator(0u); return true;
    case 0xE5u: SetAccumulator(ReadDirect(imm8())); return true;
    case 0xE6u: case 0xE7u: SetAccumulator(ReadIndirect(indir_addr(op & 1u))); return true;
    case 0xF0u: bus.WriteExternal(Dptr(), Accumulator()); return true;
    case 0xF2u: case 0xF3u: bus.WriteExternal(movx_ri_addr(op & 1u), Accumulator()); return true;
    case 0xF4u: SetAccumulator(static_cast<uint8_t>(~Accumulator())); return true;
    case 0xF5u: WriteDirect(imm8(), Accumulator()); return true;
    case 0xF6u: case 0xF7u: WriteIndirect(indir_addr(op & 1u), Accumulator()); return true;
    default:
        Unsupported(op, pc0);
        return false;
    }
}

