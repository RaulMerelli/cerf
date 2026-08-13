#include "peripheral_dispatcher.h"

#include "peripheral_base.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"

#include <algorithm>
#include <typeinfo>

REGISTER_SERVICE(PeripheralDispatcher);

std::vector<Peripheral*> PeripheralDispatcher::RegisteredPeripherals() const {
    std::vector<Peripheral*> out;
    const EntryTable* t = live_.load(std::memory_order_acquire);
    if (!t) return out;
    out.reserve(t->size());
    for (const auto& e : *t) out.push_back(e.p);
    return out;
}

void PeripheralDispatcher::Register(Peripheral* p) {
    if (!p) {
        LOG(Caution, "PeripheralDispatcher::Register called with null\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t base = p->MmioBase();
    const uint32_t size = p->MmioSize();
    const uint32_t end  = base + size;
    if (size == 0) {
        LOG(Caution, "PeripheralDispatcher::Register peripheral has "
                "zero-size MMIO range (base 0x%08X)\n", base);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const EntryTable* prev = live_.load(std::memory_order_acquire);
    if (prev) {
        for (const auto& e : *prev) {
            if (base < e.end && e.base < end) {
                LOG(Caution, "PeripheralDispatcher::Register overlap: "
                        "new [0x%08X..0x%08X) vs existing [0x%08X..0x%08X)\n",
                        base, end, e.base, e.end);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
        }
    }

    auto next = prev ? std::make_unique<EntryTable>(*prev)
                     : std::make_unique<EntryTable>();
    Entry entry{base, end, p->FastReader(), p->FastWriter(), p, p};
    auto pos = std::lower_bound(next->begin(), next->end(), base,
        [](const Entry& e, uint32_t b) { return e.base < b; });
    next->insert(pos, entry);

    const EntryTable* published = next.get();
    tables_.push_back(std::move(next));
    last_hit_.store(0, std::memory_order_relaxed);
    live_.store(published, std::memory_order_release);

    LOG(Periph, "Register 0x%08X..0x%08X\n", base, end);
}

bool PeripheralDispatcher::IsPeripheralAddress(uint32_t addr) const {
    return LookupEntry(addr) != nullptr;
}

void PeripheralDispatcher::ValidatePhysReachable(uint32_t phys_addr_mask) const {
    if (phys_addr_mask == 0xFFFFFFFFu) return;
    const EntryTable* t = live_.load(std::memory_order_acquire);
    if (!t) return;
    for (const auto& e : *t) {
        if ((e.end - 1u) > phys_addr_mask) {
            LOG(Caution, "PeripheralDispatcher: %s at [0x%08X..0x%08X) is above "
                    "the SoC physical space (mask 0x%08X); it aliases to "
                    "0x%08X and is unreachable/shadowed - relocate it into the "
                    "addressable range\n",
                    typeid(*e.p).name(), e.base, e.end, phys_addr_mask,
                    e.base & phys_addr_mask);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
}

const PeripheralDispatcher::Entry* PeripheralDispatcher::LookupEntry(
    uint32_t addr) const {
    const EntryTable* t = live_.load(std::memory_order_acquire);
    if (!t) return nullptr;

    /* QEMU system/physmem.c:345 address_space_lookup_region(), mru_section. */
    const size_t cached = last_hit_.load(std::memory_order_relaxed);
    if (cached < t->size()) {
        const Entry& hit = (*t)[cached];
        if (addr >= hit.base && addr < hit.end) return &hit;
    }
    auto it = std::upper_bound(t->begin(), t->end(), addr,
        [](uint32_t a, const Entry& e) { return a < e.base; });
    if (it == t->begin()) return nullptr;
    --it;
    if (addr >= it->base && addr < it->end) {
        last_hit_.store(static_cast<size_t>(it - t->begin()),
                        std::memory_order_relaxed);
        return &(*it);
    }
    return nullptr;
}

uint8_t PeripheralDispatcher::ReadByte(uint32_t addr) {
    if (const Entry* e = LookupEntry(addr)) {
        return static_cast<uint8_t>(e->read(e->ctx, addr - e->base, 1));
    }
    return emu_.Get<EmulatedMemory>().ReadByte(addr);
}

uint16_t PeripheralDispatcher::ReadHalf(uint32_t addr) {
    if (const Entry* e = LookupEntry(addr)) {
        return static_cast<uint16_t>(e->read(e->ctx, addr - e->base, 2));
    }
    return emu_.Get<EmulatedMemory>().ReadHalf(addr);
}

uint32_t PeripheralDispatcher::ReadWord(uint32_t addr) {
    if (const Entry* e = LookupEntry(addr)) {
        return e->read(e->ctx, addr - e->base, 4);
    }
    return emu_.Get<EmulatedMemory>().ReadWord(addr);
}

uint64_t PeripheralDispatcher::ReadDword(uint32_t addr) {
    if (const Entry* e = LookupEntry(addr)) {
        return e->p->ReadDword(addr);
    }
    return emu_.Get<EmulatedMemory>().ReadDword(addr);
}

void PeripheralDispatcher::WriteByte(uint32_t addr, uint8_t value) {
    if (const Entry* e = LookupEntry(addr)) {
        e->write(e->ctx, addr - e->base, value, 1);
        return;
    }
    emu_.Get<EmulatedMemory>().WriteByte(addr, value);
}

void PeripheralDispatcher::WriteHalf(uint32_t addr, uint16_t value) {
    if (const Entry* e = LookupEntry(addr)) {
        e->write(e->ctx, addr - e->base, value, 2);
        return;
    }
    emu_.Get<EmulatedMemory>().WriteHalf(addr, value);
}

void PeripheralDispatcher::WriteWord(uint32_t addr, uint32_t value) {
    if (const Entry* e = LookupEntry(addr)) {
        e->write(e->ctx, addr - e->base, value, 4);
        return;
    }
    emu_.Get<EmulatedMemory>().WriteWord(addr, value);
}

void PeripheralDispatcher::WriteDword(uint32_t addr, uint64_t value) {
    if (const Entry* e = LookupEntry(addr)) {
        e->p->WriteDword(addr, value);
        return;
    }
    emu_.Get<EmulatedMemory>().WriteDword(addr, value);
}

