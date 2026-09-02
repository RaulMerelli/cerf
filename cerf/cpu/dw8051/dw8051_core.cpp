#include "dw8051_core.h"

#include "../../core/log.h"

bool Dw8051Core::RunInstruction(Dw8051Bus& bus, bool external_interrupt0_pending) {
    if (sfr_.Halted()) return false;
    sfr_.ServiceInterrupts(external_interrupt0_pending);
    if (!Step(bus)) return false;
    sfr_.AdvanceTimers();
    sfr_.ServiceInterrupts(external_interrupt0_pending);
    return true;
}

void Dw8051Core::Call(uint16_t target) {
    const uint16_t ret = sfr_.Pc();
    sfr_.Push(static_cast<uint8_t>(ret));
    sfr_.Push(static_cast<uint8_t>(ret >> 8));
    sfr_.SetPc(target);
}

void Dw8051Core::Return() {
    const uint8_t high = sfr_.Pop();
    const uint8_t low = sfr_.Pop();
    sfr_.SetPc(static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low));
}

[[noreturn]] void Dw8051Core::Unsupported(uint8_t opcode, uint16_t pc) {
    sfr_.Halt(opcode);
    LOG(Caution, "MCS-51: unsupported opcode=0x%02X pc=0x%04X executed=%llu unsupported=%llu\n", opcode, pc,
        static_cast<unsigned long long>(sfr_.Executed()), static_cast<unsigned long long>(sfr_.UnsupportedCount()));
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

uint16_t Dw8051Core::AjmpTarget(uint8_t opcode, uint8_t low) const {
    return static_cast<uint16_t>((sfr_.Pc() & 0xF800u) | (static_cast<uint16_t>(opcode & 0xE0u) << 3) | low);
}

void Dw8051Core::Add(uint8_t value, bool with_carry) {
    const uint8_t a = sfr_.Accumulator();
    const uint8_t carry = (with_carry && sfr_.Carry()) ? 1u : 0u;
    const uint16_t sum = static_cast<uint16_t>(a) + value + carry;
    uint8_t psw = static_cast<uint8_t>(sfr_.Psw() & ~(0x80u | 0x40u | 0x04u));
    if (sum > 0xFFu) psw |= 0x80u;
    if (((a & 0x0Fu) + (value & 0x0Fu) + carry) > 0x0Fu) psw |= 0x40u;
    const uint8_t result = static_cast<uint8_t>(sum);
    if (((~(a ^ value)) & (a ^ result) & 0x80u) != 0u) psw |= 0x04u;
    sfr_.SetPsw(psw);
    sfr_.SetAccumulator(result);
}

void Dw8051Core::Subb(uint8_t value) {
    const uint8_t a = sfr_.Accumulator();
    const uint8_t carry = sfr_.Carry() ? 1u : 0u;
    const uint16_t rhs = static_cast<uint16_t>(value) + carry;
    const uint16_t difference = static_cast<uint16_t>(a) - rhs;
    uint8_t psw = static_cast<uint8_t>(sfr_.Psw() & ~(0x80u | 0x40u | 0x04u));
    if (static_cast<uint16_t>(a) < rhs) psw |= 0x80u;
    if ((a & 0x0Fu) < ((value & 0x0Fu) + carry)) psw |= 0x40u;
    const uint8_t result = static_cast<uint8_t>(difference);
    if (((a ^ value) & (a ^ result) & 0x80u) != 0u) psw |= 0x04u;
    sfr_.SetPsw(psw);
    sfr_.SetAccumulator(result);
}

void Dw8051Core::Compare(uint8_t lhs, uint8_t rhs) {
    sfr_.SetCarry(lhs < rhs);
}

bool Dw8051Core::Step(Dw8051Bus& bus) {
    if (sfr_.Halted()) return false;
    const uint16_t pc0 = sfr_.Pc();
    const uint8_t op = bus.FetchCode(sfr_.FetchPc());
    sfr_.BumpExecuted();

    auto imm8 = [this, &bus]() -> uint8_t { return bus.FetchCode(sfr_.FetchPc()); };
    auto imm16 = [&]() -> uint16_t {
        const uint8_t high = imm8();
        const uint8_t low = imm8();
        return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
    };
    auto rel8 = [&]() -> int8_t { return static_cast<int8_t>(imm8()); };
    auto branch = [&](int8_t rel) { sfr_.SetPc(static_cast<uint16_t>(sfr_.Pc() + rel)); };
    auto indir_addr = [&](uint8_t r) -> uint8_t { return sfr_.Register(r); };
    auto movx_ri_addr = [&](uint8_t r) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(sfr_.Port2()) << 8) | sfr_.Register(r));
    };

    if ((op & 0x1Fu) == 0x01u) {
        const uint8_t low = imm8();
        sfr_.SetPc(AjmpTarget(op, low));
        return true;
    }
    if ((op & 0x1Fu) == 0x11u) {
        const uint8_t low = imm8();
        Call(AjmpTarget(op, low));
        return true;
    }
    if (op >= 0x08u && op <= 0x0Fu) {
        const uint8_t r = op & 7u;
        sfr_.SetRegister(r, static_cast<uint8_t>(sfr_.Register(r) + 1u));
        return true;
    }
    if (op >= 0x18u && op <= 0x1Fu) {
        const uint8_t r = op & 7u;
        sfr_.SetRegister(r, static_cast<uint8_t>(sfr_.Register(r) - 1u));
        return true;
    }
    if (op >= 0x28u && op <= 0x2Fu) {
        Add(sfr_.Register(op & 7u), false);
        return true;
    }
    if (op >= 0x38u && op <= 0x3Fu) {
        Add(sfr_.Register(op & 7u), true);
        return true;
    }
    if (op >= 0x48u && op <= 0x4Fu) {
        sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() | sfr_.Register(op & 7u)));
        return true;
    }
    if (op >= 0x58u && op <= 0x5Fu) {
        sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() & sfr_.Register(op & 7u)));
        return true;
    }
    if (op >= 0x68u && op <= 0x6Fu) {
        sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() ^ sfr_.Register(op & 7u)));
        return true;
    }
    if (op >= 0x78u && op <= 0x7Fu) {
        sfr_.SetRegister(op & 7u, imm8());
        return true;
    }
    if (op >= 0x88u && op <= 0x8Fu) {
        sfr_.WriteDirect(imm8(), sfr_.Register(op & 7u));
        return true;
    }
    if (op >= 0x98u && op <= 0x9Fu) {
        Subb(sfr_.Register(op & 7u));
        return true;
    }
    if (op >= 0xA8u && op <= 0xAFu) {
        sfr_.SetRegister(op & 7u, sfr_.ReadDirect(imm8()));
        return true;
    }
    if (op >= 0xB8u && op <= 0xBFu) {
        const uint8_t rhs = imm8();
        const int8_t rel = rel8();
        const uint8_t lhs = sfr_.Register(op & 7u);
        Compare(lhs, rhs);
        if (lhs != rhs) branch(rel);
        return true;
    }
    if (op >= 0xC8u && op <= 0xCFu) {
        const uint8_t r = op & 7u;
        const uint8_t tmp = sfr_.Accumulator();
        sfr_.SetAccumulator(sfr_.Register(r));
        sfr_.SetRegister(r, tmp);
        return true;
    }
    if (op >= 0xD8u && op <= 0xDFu) {
        const uint8_t r = op & 7u;
        const int8_t rel = rel8();
        const uint8_t v = static_cast<uint8_t>(sfr_.Register(r) - 1u);
        sfr_.SetRegister(r, v);
        if (v != 0u) branch(rel);
        return true;
    }
    if (op >= 0xE8u && op <= 0xEFu) {
        sfr_.SetAccumulator(sfr_.Register(op & 7u));
        return true;
    }
    if (op >= 0xF8u && op <= 0xFFu) {
        sfr_.SetRegister(op & 7u, sfr_.Accumulator());
        return true;
    }

    switch (op) {
    case 0x00u: return true;
    case 0x02u: sfr_.SetPc(imm16()); return true;
    case 0x03u: {
        const uint8_t a = sfr_.Accumulator();
        sfr_.SetAccumulator(static_cast<uint8_t>((a >> 1) | (a << 7)));
        return true;
    }
    case 0x04u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() + 1u)); return true;
    case 0x05u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) + 1u));
        return true;
    }
    case 0x06u:
    case 0x07u: {
        const uint8_t a = indir_addr(op & 1u);
        sfr_.WriteIndirect(a, static_cast<uint8_t>(sfr_.ReadIndirect(a) + 1u));
        return true;
    }
    case 0x10u: {
        const uint8_t b = imm8();
        const int8_t r = rel8();
        if (sfr_.ReadBit(b)) {
            sfr_.WriteBit(b, false);
            branch(r);
        }
        return true;
    }
    case 0x12u: Call(imm16()); return true;
    case 0x13u: {
        const uint8_t a = sfr_.Accumulator();
        const bool c = sfr_.Carry();
        sfr_.SetCarry((a & 1u) != 0u);
        sfr_.SetAccumulator(static_cast<uint8_t>((a >> 1) | (c ? 0x80u : 0u)));
        return true;
    }
    case 0x14u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() - 1u)); return true;
    case 0x15u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) - 1u));
        return true;
    }
    case 0x16u:
    case 0x17u: {
        const uint8_t a = indir_addr(op & 1u);
        sfr_.WriteIndirect(a, static_cast<uint8_t>(sfr_.ReadIndirect(a) - 1u));
        return true;
    }
    case 0x20u: {
        const uint8_t b = imm8();
        const int8_t r = rel8();
        if (sfr_.ReadBit(b)) branch(r);
        return true;
    }
    case 0x22u: Return(); return true;
    case 0x23u: {
        const uint8_t a = sfr_.Accumulator();
        sfr_.SetAccumulator(static_cast<uint8_t>((a << 1) | (a >> 7)));
        return true;
    }
    case 0x24u: Add(imm8(), false); return true;
    case 0x25u: Add(sfr_.ReadDirect(imm8()), false); return true;
    case 0x26u:
    case 0x27u: Add(sfr_.ReadIndirect(indir_addr(op & 1u)), false); return true;
    case 0x30u: {
        const uint8_t b = imm8();
        const int8_t r = rel8();
        if (!sfr_.ReadBit(b)) branch(r);
        return true;
    }
    case 0x32u:
        Return();
        sfr_.LeaveInterrupt();
        return true;
    case 0x33u: {
        const uint8_t a = sfr_.Accumulator();
        const bool c = sfr_.Carry();
        sfr_.SetCarry((a & 0x80u) != 0u);
        sfr_.SetAccumulator(static_cast<uint8_t>((a << 1) | (c ? 1u : 0u)));
        return true;
    }
    case 0x34u: Add(imm8(), true); return true;
    case 0x35u: Add(sfr_.ReadDirect(imm8()), true); return true;
    case 0x36u:
    case 0x37u: Add(sfr_.ReadIndirect(indir_addr(op & 1u)), true); return true;
    case 0x40u: {
        const int8_t r = rel8();
        if (sfr_.Carry()) branch(r);
        return true;
    }
    case 0x42u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) | sfr_.Accumulator()));
        return true;
    }
    case 0x43u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) | imm8()));
        return true;
    }
    case 0x44u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() | imm8())); return true;
    case 0x45u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() | sfr_.ReadDirect(imm8()))); return true;
    case 0x46u:
    case 0x47u:
        sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() | sfr_.ReadIndirect(indir_addr(op & 1u))));
        return true;
    case 0x50u: {
        const int8_t r = rel8();
        if (!sfr_.Carry()) branch(r);
        return true;
    }
    case 0x52u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) & sfr_.Accumulator()));
        return true;
    }
    case 0x53u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) & imm8()));
        return true;
    }
    case 0x54u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() & imm8())); return true;
    case 0x55u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() & sfr_.ReadDirect(imm8()))); return true;
    case 0x56u:
    case 0x57u:
        sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() & sfr_.ReadIndirect(indir_addr(op & 1u))));
        return true;
    case 0x60u: {
        const int8_t r = rel8();
        if (sfr_.Accumulator() == 0u) branch(r);
        return true;
    }
    case 0x62u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) ^ sfr_.Accumulator()));
        return true;
    }
    case 0x63u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, static_cast<uint8_t>(sfr_.ReadDirect(d) ^ imm8()));
        return true;
    }
    case 0x64u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() ^ imm8())); return true;
    case 0x65u: sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() ^ sfr_.ReadDirect(imm8()))); return true;
    case 0x66u:
    case 0x67u:
        sfr_.SetAccumulator(static_cast<uint8_t>(sfr_.Accumulator() ^ sfr_.ReadIndirect(indir_addr(op & 1u))));
        return true;
    case 0x70u: {
        const int8_t r = rel8();
        if (sfr_.Accumulator() != 0u) branch(r);
        return true;
    }
    case 0x72u: sfr_.SetCarry(sfr_.Carry() || sfr_.ReadBit(imm8())); return true;
    case 0x73u: sfr_.SetPc(static_cast<uint16_t>(sfr_.Dptr() + sfr_.Accumulator())); return true;
    case 0x74u: sfr_.SetAccumulator(imm8()); return true;
    case 0x75u: {
        const uint8_t d = imm8();
        sfr_.WriteDirect(d, imm8());
        return true;
    }
    case 0x76u:
    case 0x77u: sfr_.WriteIndirect(indir_addr(op & 1u), imm8()); return true;
    case 0x80u: branch(rel8()); return true;
    case 0x82u: sfr_.SetCarry(sfr_.Carry() && sfr_.ReadBit(imm8())); return true;
    case 0x83u: {
        const uint16_t addr = static_cast<uint16_t>(sfr_.Pc() + sfr_.Accumulator());
        sfr_.SetAccumulator(bus.FetchCode(addr));
        return true;
    }
    case 0x84u: {
        const uint8_t divisor = sfr_.B();
        const uint16_t q = divisor ? (sfr_.Accumulator() / divisor) : 0u;
        const uint16_t r = divisor ? (sfr_.Accumulator() % divisor) : 0u;
        sfr_.SetPsw(static_cast<uint8_t>(sfr_.Psw() & ~0x80u));
        if (divisor == 0u)
            sfr_.SetPsw(static_cast<uint8_t>(sfr_.Psw() | 0x04u));
        else
            sfr_.SetPsw(static_cast<uint8_t>(sfr_.Psw() & ~0x04u));
        sfr_.SetAccumulator(static_cast<uint8_t>(q));
        sfr_.SetB(static_cast<uint8_t>(r));
        return true;
    }
    case 0x85u: {
        const uint8_t src = imm8();
        const uint8_t dst = imm8();
        sfr_.WriteDirect(dst, sfr_.ReadDirect(src));
        return true;
    }
    case 0x86u:
    case 0x87u: sfr_.WriteDirect(imm8(), sfr_.ReadIndirect(indir_addr(op & 1u))); return true;
    case 0x90u: sfr_.SetDptr(imm16()); return true;
    case 0x92u: sfr_.WriteBit(imm8(), sfr_.Carry()); return true;
    case 0x93u:
        sfr_.SetAccumulator(bus.FetchCode(static_cast<uint16_t>(sfr_.Dptr() + sfr_.Accumulator())));
        return true;
    case 0x94u: Subb(imm8()); return true;
    case 0x95u: Subb(sfr_.ReadDirect(imm8())); return true;
    case 0x96u:
    case 0x97u: Subb(sfr_.ReadIndirect(indir_addr(op & 1u))); return true;
    case 0xA0u: sfr_.SetCarry(sfr_.Carry() || !sfr_.ReadBit(imm8())); return true;
    case 0xA2u: sfr_.SetCarry(sfr_.ReadBit(imm8()) != 0u); return true;
    case 0xA3u: sfr_.SetDptr(static_cast<uint16_t>(sfr_.Dptr() + 1u)); return true;
    case 0xA4u: {
        const uint16_t prod = static_cast<uint16_t>(sfr_.Accumulator()) * sfr_.B();
        sfr_.SetAccumulator(static_cast<uint8_t>(prod));
        sfr_.SetB(static_cast<uint8_t>(prod >> 8));
        sfr_.SetPsw(static_cast<uint8_t>(sfr_.Psw() & ~0x80u));
        if (sfr_.B())
            sfr_.SetPsw(static_cast<uint8_t>(sfr_.Psw() | 0x04u));
        else
            sfr_.SetPsw(static_cast<uint8_t>(sfr_.Psw() & ~0x04u));
        return true;
    }
    case 0xA6u:
    case 0xA7u: sfr_.WriteIndirect(indir_addr(op & 1u), sfr_.ReadDirect(imm8())); return true;
    case 0xB0u: sfr_.SetCarry(sfr_.Carry() && !sfr_.ReadBit(imm8())); return true;
    case 0xB2u: {
        const uint8_t b = imm8();
        sfr_.WriteBit(b, !sfr_.ReadBit(b));
        return true;
    }
    case 0xB3u: sfr_.SetCarry(!sfr_.Carry()); return true;
    case 0xB4u: {
        const uint8_t rhs = imm8();
        const int8_t r = rel8();
        const uint8_t lhs = sfr_.Accumulator();
        Compare(lhs, rhs);
        if (lhs != rhs) branch(r);
        return true;
    }
    case 0xB5u: {
        const uint8_t rhs = sfr_.ReadDirect(imm8());
        const int8_t r = rel8();
        const uint8_t lhs = sfr_.Accumulator();
        Compare(lhs, rhs);
        if (lhs != rhs) branch(r);
        return true;
    }
    case 0xB6u:
    case 0xB7u: {
        const uint8_t lhs = sfr_.ReadIndirect(indir_addr(op & 1u));
        const uint8_t rhs = imm8();
        const int8_t r = rel8();
        Compare(lhs, rhs);
        if (lhs != rhs) branch(r);
        return true;
    }
    case 0xC0u: sfr_.Push(sfr_.ReadDirect(imm8())); return true;
    case 0xC2u: sfr_.WriteBit(imm8(), false); return true;
    case 0xC3u: sfr_.SetCarry(false); return true;
    case 0xC4u: {
        const uint8_t a = sfr_.Accumulator();
        sfr_.SetAccumulator(static_cast<uint8_t>((a << 4) | (a >> 4)));
        return true;
    }
    case 0xC5u: {
        const uint8_t d = imm8();
        const uint8_t tmp = sfr_.Accumulator();
        sfr_.SetAccumulator(sfr_.ReadDirect(d));
        sfr_.WriteDirect(d, tmp);
        return true;
    }
    case 0xC6u:
    case 0xC7u: {
        const uint8_t a = indir_addr(op & 1u);
        const uint8_t tmp = sfr_.Accumulator();
        sfr_.SetAccumulator(sfr_.ReadIndirect(a));
        sfr_.WriteIndirect(a, tmp);
        return true;
    }
    case 0xD0u: sfr_.WriteDirect(imm8(), sfr_.Pop()); return true;
    case 0xD2u: sfr_.WriteBit(imm8(), true); return true;
    case 0xD3u: sfr_.SetCarry(true); return true;
    case 0xD4u: {
        uint8_t a = sfr_.Accumulator();
        bool c = sfr_.Carry();
        if ((a & 0x0Fu) > 9u || (sfr_.Psw() & 0x40u)) a = static_cast<uint8_t>(a + 0x06u);
        if ((a & 0xF0u) > 0x90u || c) {
            a = static_cast<uint8_t>(a + 0x60u);
            c = true;
        }
        sfr_.SetAccumulator(a);
        sfr_.SetCarry(c);
        return true;
    }
    case 0xD5u: {
        const uint8_t d = imm8();
        const int8_t r = rel8();
        const uint8_t v = static_cast<uint8_t>(sfr_.ReadDirect(d) - 1u);
        sfr_.WriteDirect(d, v);
        if (v != 0u) branch(r);
        return true;
    }
    case 0xD6u:
    case 0xD7u: {
        const uint8_t a = indir_addr(op & 1u);
        const uint8_t mem = sfr_.ReadIndirect(a);
        const uint8_t acc = sfr_.Accumulator();
        sfr_.WriteIndirect(a, static_cast<uint8_t>((mem & 0xF0u) | (acc & 0x0Fu)));
        sfr_.SetAccumulator(static_cast<uint8_t>((acc & 0xF0u) | (mem & 0x0Fu)));
        return true;
    }
    case 0xE0u: sfr_.SetAccumulator(bus.ReadExternal(sfr_.Dptr())); return true;
    case 0xE2u:
    case 0xE3u: sfr_.SetAccumulator(bus.ReadExternal(movx_ri_addr(op & 1u))); return true;
    case 0xE4u: sfr_.SetAccumulator(0u); return true;
    case 0xE5u: sfr_.SetAccumulator(sfr_.ReadDirect(imm8())); return true;
    case 0xE6u:
    case 0xE7u: sfr_.SetAccumulator(sfr_.ReadIndirect(indir_addr(op & 1u))); return true;
    case 0xF0u: bus.WriteExternal(sfr_.Dptr(), sfr_.Accumulator()); return true;
    case 0xF2u:
    case 0xF3u: bus.WriteExternal(movx_ri_addr(op & 1u), sfr_.Accumulator()); return true;
    case 0xF4u: sfr_.SetAccumulator(static_cast<uint8_t>(~sfr_.Accumulator())); return true;
    case 0xF5u: sfr_.WriteDirect(imm8(), sfr_.Accumulator()); return true;
    case 0xF6u:
    case 0xF7u: sfr_.WriteIndirect(indir_addr(op & 1u), sfr_.Accumulator()); return true;
    default: Unsupported(op, pc0);
    }
}
